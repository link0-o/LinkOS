%include"boot.inc"

section loader vstart=LOADER_BASE_ADDR      ;代码段起始位置
LOADER_STACK_TOP equ LOADER_BASE_ADDR       ;用一个宏去记录
jmp loader_start                            ;跳转

;---------构建GDT(全局描述符)------------
    GDT_BASE:   dd  0x00000000
                dd  0x00000000              ;下标索引为0的段描述符必须为0(规定)

    CODE_DESC:  dd  0x0000FFFF              ;加载地址是0
                dd  DESC_CODE_HIGH4       ;G位为1,代表粗颗粒(4KB),配合FFFFF可以寻址内存地址到4GB
            
    DATA_DESC:  dd  0x00000FFFF
                dd  DESC_DATA_HIGH4       ;同上,但是访问权限不一样

    VIDEO_DESC: dd  0x80000007              ;limit=(0xbffff-0xb8000)/4k=0x7
                dd  DESC_VIDEO_HIGH4        ;此时 dpl 为 0

;---------  定义一些宏,来用于方便索引和描述 -----------
GDT_SIZE    equ $ - GDT_BASE
GDT_LIMIT   equ GDT_SIZE - 1

times 60 dq 0    ; 此处预留 60 个描述符的空位

;---------  定义段选择子(保护模式)  ---------
SELECTOR_CODE   equ (0x0001 << 3) + TI_GDT + RPL0    ; 相当于 (CODE_DESC - GDT_BASE) / 8 + TI_GDT + RPL0
SELECTOR_DATA   equ (0x0002 << 3) + TI_GDT + RPL0    ; 同上
SELECTOR_VIDEO  equ (0x0003 << 3) + TI_GDT + RPL0    ; 同上

gdt_ptr:
    dw GDT_LIMIT        ; GDT 界限（2 字节）
    dd GDT_BASE         ; GDT 基地址（4 字节）

loadermsg db 'hello loader!'

loader_start:
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;--------   打印字符串  ----------;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
    ; 设置栈指针
    mov sp, LOADER_BASE_ADDR

    mov bp, loadermsg          ; ES:BP -> 字符串地址
    mov cx, 13                 ; 字符串长度
    mov ax, 0x1301             ; 功能号：AH=13h, AL=01h（显示字符串并移动光标）
    mov bx, 0x0053             ; BH=0（页号），BL=0x53 → 粉红底 + 青色字
    mov dx, 0x1800             ; DH=24行, DL=0列
    int 0x10                   ; 调用BIOS视频中断

    xchg bx,bx         ;魔术断点

;---------- 准备进入保护模式 ----------
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;1 打开 A20
;2 加载 gdt
;3 将 cr0 的 pe 位置 1

;----------------- 打开 A20 ----------------
    in  al,0x92
    or  al,0000_0001B
    out 0x92,al

;----------------- 加载 GDT ----------------
    lgdt [gdt_ptr]

;----------------- cr0 第 0 位置 1 ----------------
    mov eax, cr0
    or eax, 0x00000001
    mov cr0, eax

    jmp dword SELECTOR_CODE:p_mode_start    ; 刷新流水线

[bits 32]
p_mode_start:
    mov ax, SELECTOR_DATA
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, LOADER_STACK_TOP

    mov ax, SELECTOR_VIDEO
    mov gs, ax

    mov byte [gs:160], 'P'

    jmp $
