#include "keyboard.h"
#include "io.h"
#include "print.h"
#include "interrupt.h"
#include "global.h"
#include "ioqueue.h"


#define KBD_BUF_PORT    0x60    // 键盘 buffer 寄存器端口号为 0x60

/* 用转义字符定义部分控制字符 */
#define esc     '\033'       // 八进制表示字符，也可以用十六进制'\x1b'
#define backspace '\b'       
#define tab     '\t'
#define enter   '\n'
#define delete  '\x7f'

/* 以上不可见字符一律定义为 0 */
#define char_invisible 0
#define ctrl_l_char  char_invisible
#define ctrl_r_char  char_invisible
#define shift_l_char char_invisible
#define shift_r_char char_invisible
#define alt_l_char   char_invisible
#define alt_r_char   char_invisible
#define caps_lock_char char_invisible

/* 定义控制字符的通码和断码 */
#define shift_l_make 0x2a
#define shift_r_make 0x36
#define alt_l_make   0x38
#define alt_r_make   0xe038
#define alt_r_break  0xe0b8
#define ctrl_l_make  0x1d
#define ctrl_r_make  0xe01d
#define ctrl_r_break 0xe09d
#define caps_lock_make 0x3a

/* 方向键的扫描码 */
#define arrow_up_make    0xe048
#define arrow_down_make  0xe050
#define arrow_left_make  0xe04b
#define arrow_right_make 0xe04d

/* 其他常用扩展键 */
#define home_make        0xe047
#define end_make         0xe04f
#define page_up_make     0xe049
#define page_down_make   0xe051
#define delete_make      0xe053
#define insert_make      0xe052

struct ioqueue kbd_buf;     // 定义键盘缓冲区


/* 定义以下变量记录相应键是否按下的状态，按下时值为 true，否则为 false */
static bool ctrl_status, shift_status, alt_status, caps_lock_status;

/* 以通码 make_code 为索引的二维数组 */
static char keymap[][2] = {
/* 扫描码未与 shift 组合时的字符，与 shift 组合时的字符 */
/* ------------------------------------------ */
/* 0x00 */    {0,     0},
/* 0x01 */    {esc,   esc},
/* 0x02 */    {'1',   '!'},
/* 0x03 */    {'2',   '@'},
/* 0x04 */    {'3',   '#'},
/* 0x05 */    {'4',   '$'},
/* 0x06 */    {'5',   '%'},
/* 0x07 */    {'6',   '^'},
/* 0x08 */    {'7',   '&'},
/* 0x09 */    {'8',   '*'},
/* 0x0A */    {'9',   '('},
/* 0x0B */    {'0',   ')'},
/* 0x0C */    {'-',   '_'},
/* 0x0D */    {'=',   '+'},
/* 0x0E */    {backspace, backspace},
/* 0x0F */    {tab,   tab},
/* 0x10 */    {'q',   'Q'},
/* 0x11 */    {'w',   'W'},
/* 0x12 */    {'e',   'E'},
/* 0x13 */    {'r',   'R'},
/* 0x14 */    {'t',   'T'},
/* 0x15 */    {'y',   'Y'},
/* 0x16 */    {'u',   'U'},
/* 0x17 */    {'i',   'I'},
/* 0x18 */    {'o',   'O'},
/* 0x19 */    {'p',   'P'},
/* 0x1A */    {'[',   '{'},
/* 0x1B */    {']',   '}'},
/* 0x1C */    {enter, enter},
/* 0x1D */    {ctrl_l_char, ctrl_l_char},
/* 0x1E */    {'a',   'A'},
/* 0x1F */    {'s',   'S'},
/* 0x20 */    {'d',   'D'},
/* 0x21 */    {'f',   'F'},
/* 0x22 */    {'g',   'G'},
/* 0x23 */    {'h',   'H'},
/* 0x24 */    {'j',   'J'},
/* 0x25 */    {'k',   'K'},
/* 0x26 */    {'l',   'L'},
/* 0x27 */    {';',   ':'},
/* 0x28 */    {'\'',  '"'},
/* 0x29 */    {'`',   '~'},
/* 0x2A */    {shift_l_char, shift_l_char},
/* 0x2B */    {'\\',  '|'},
/* 0x2C */    {'z',   'Z'},
/* 0x2D */    {'x',   'X'},
/* 0x2E */    {'c',   'C'},
/* 0x2F */    {'v',   'V'},
/* 0x30 */    {'b',   'B'},
/* 0x31 */    {'n',   'N'},
/* 0x32 */    {'m',   'M'},
/* 0x33 */    {',',   '<'},
/* 0x34 */    {'.',   '>'},
/* 0x35 */    {'/',   '?'},
/* 0x36 */    {shift_r_char, shift_r_char},
/* 0x37 */    {'*',   '*'},
/* 0x38 */    {alt_l_char, alt_l_char},
/* 0x39 */    {' ',   ' '},
/* 0x3A */    {caps_lock_char, caps_lock_char}
/* 其余的扫描码暂不处理 */
};

/* 判断扫描码是否为断码 (最高位为1表示断码) */
static bool is_break_code(uint8_t scancode) {
    return (scancode & 0x80) != 0;
}

/* 键盘中断处理程序 */
static void intr_keyboard_handler(uint8_t vec_nr){
    (void)vec_nr;  // 避免未使用参数警告
    
   
    uint8_t scancode = inb(KBD_BUF_PORT);
    
    /* 处理扩展扫描码 (0xE0 开头的双字节扫描码) */
    static bool ext_scancode = false;  // 记录上一次是否是 0xE0
    static uint16_t full_scancode = 0; // 完整的扫描码(8/16位)
    
    if (scancode == 0xe0) {
        ext_scancode = true;
        return;
    }
    
    /* 完整的扫描码 */
    if (ext_scancode) {
        full_scancode = 0xe000 | scancode;
        ext_scancode = false;
    } else {
        full_scancode = scancode;
    }
    
    /* 判断是通码or断码 */
    bool is_break = is_break_code(full_scancode);

    /* 处理 Shift */
    if (full_scancode == shift_l_make || full_scancode == shift_r_make) {
        shift_status = true;
        return;
    } else if (full_scancode == (shift_l_make | 0x80) || full_scancode == (shift_r_make | 0x80)) {
        shift_status = false;
        return;
    }
    
    /* 处理 Ctrl */
    if (full_scancode == ctrl_l_make || full_scancode == ctrl_r_make) {
        ctrl_status = true;
        return;
    } else if (full_scancode == (ctrl_l_make | 0x80) || full_scancode == ctrl_r_break) {
        ctrl_status = false;
        return;
    }
    
    /* 处理 Alt */
    if (full_scancode == alt_l_make || full_scancode == alt_r_make) {
        alt_status = true;
        return;
    } else if (full_scancode == (alt_l_make | 0x80) || full_scancode == alt_r_break) {
        alt_status = false;
        return;
    }

    /* 处理 Caps Lock (按下时切换状态) */
    if (full_scancode == caps_lock_make) {
        caps_lock_status = !caps_lock_status;
        return;
    }
    
    /* 只处理通码,忽略断码 */
    if (is_break) {
        return;
    }
    
    /* 处理方向键和其他扩展键 */
    switch (full_scancode) {
        case arrow_up_make: {
            /* ↑ */
            uint32_t cursor_pos = get_cursor();
            if (cursor_pos >= 80) {  
                set_cursor(cursor_pos - 80);
            }
            return;
        }
        case arrow_down_make: {
            /* ↓ */
            uint32_t cursor_pos = get_cursor();
            if (cursor_pos < 80 * 24) {
                set_cursor(cursor_pos + 80);
            }
            return;
        }
        case arrow_left_make: {
            /* ← */
            uint32_t cursor_pos = get_cursor();
            if (cursor_pos > 0) {
                set_cursor(cursor_pos - 1);
            }
            return;
        }
        case arrow_right_make: {
            /* → */
            uint32_t cursor_pos = get_cursor();
            if (cursor_pos < 80 * 25 - 1) {  // 不超过屏幕末尾
                set_cursor(cursor_pos + 1);
            }
            return;
        }
        case home_make: {
            /* Home: 光标移到行首 */
            uint32_t cursor_pos = get_cursor();
            uint32_t row = cursor_pos / 80;
            set_cursor(row * 80);
            return;
        }
        case end_make: {
            /* End: 光标移到行尾 */
            uint32_t cursor_pos = get_cursor();
            uint32_t row = cursor_pos / 80;
            set_cursor(row * 80 + 79);
            return;
        }
        case page_up_make: {
            /* Page Up: 向上翻页(向上移动5行) */
            uint32_t cursor_pos = get_cursor();
            if (cursor_pos >= 80 * 5) {
                set_cursor(cursor_pos - 80 * 5);
            } else {
                set_cursor(0);  // 移到开头
            }
            return;
        }
        case page_down_make: {
            /* Page Down: 向下翻页(向下移动5行) */
            uint32_t cursor_pos = get_cursor();
            if (cursor_pos < 80 * 20) {
                set_cursor(cursor_pos + 80 * 5);
            }
            return;
        }
        case delete_make: {
            /* Delete: 删除光标位置的字符(暂时输出空格覆盖) */
            uint32_t cursor_pos = get_cursor();
            put_char(' ');
            set_cursor(cursor_pos);  // 恢复光标位置
            return;
        }
        case insert_make: {
            /* Insert: 切换插入/覆盖模式(暂时不实现) */
            return;
        }
        default:
            break;
    }
    
    /* 处理普通字符 */
    if (full_scancode < 0x3B) {
        bool shift = shift_status;
        
        /* 处理大小写 */
        if ((full_scancode >= 0x10 && full_scancode <= 0x19) ||  // Q-P
            (full_scancode >= 0x1e && full_scancode <= 0x26) ||  // A-L
            (full_scancode >= 0x2c && full_scancode <= 0x32)) {  // Z-M
            
            /* Caps Lock 会反转 Shift */
            if (caps_lock_status) {
                shift = !shift;
            }
        }
        
        int shift_index = shift ? 1 : 0;
        char ch = keymap[full_scancode][shift_index];
        
        if (ch != 0) {
            if (!ioq_full(&kbd_buf)){
                put_char(ch);
                ioq_putchar(&kbd_buf, ch);
            }
        }
    }
    
    return;
}


/* 键盘初始化 */
void keyboard_init(){
    put_str("keyboard_init start\n");
    
    ioqueue_init(&kbd_buf);  // 初始化键盘缓冲区
    /* 注册键盘中断处理函数 */
    register_handler(0x21, intr_keyboard_handler);
    
    put_str("keyboard_init done\n");
}
