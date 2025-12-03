#include "timer.h"
#include "io.h"
#include "print.h"

#define IRQ0_FREQUENCY      100                 //每秒100次中断,10ms一次
#define INPUT_FREQUENCY     1193180
#define COUNTER0_VALUE      INPUT_FREQUENCY / IRQ0_FREQUENCY        //中断发生频率
#define COUNTER0_PORT       0x40                //计数器 0 的数据端口地址
#define COUNTER0_NO         0
#define COUNTER_MODE        2                   //工作方式2: 比率发生器
#define READ_WRITE_LATCH    3                   //读写格式为先写低 8 位，再写高 8 位(因为x86是小端序)
#define PIT_CONTROL_PORT    0x43                //控制寄存器

/* 把操作的计数器 counter_no､ 读写锁属性 rwl､ 计数器模式
counter_mode 写入模式控制寄存器并赋予初始值 counter_value */
static void frequency_set(uint8_t counter_port, \
                          uint8_t counter_no, \
                          uint8_t rwl, \
                          uint8_t counter_mode, \
                          uint16_t counter_value){
    /* 往控制字寄存器端口 0x43 中写入控制字 */
    outb(PIT_CONTROL_PORT, (uint8_t)(counter_no << 6 | rwl << 4 | counter_mode << 1));
    /* 先写入 counter_value 的低 8 位 */
    outb(counter_port, (uint8_t)counter_value);
    /* 再写入 counter_value 的高 8 位 */
    outb(counter_port, (uint8_t)counter_value >> 8);
}

/* 初始化 PIT8253 */
void timer_init(){
    put_str("timer_init start\n");
    /* 设置 8253 的定时周期，也就是发中断的周期 */
    frequency_set(COUNTER0_PORT, \
                  COUNTER0_NO, \
                  READ_WRITE_LATCH,\
                  COUNTER_MODE, \
                  COUNTER0_VALUE);
                  
    put_str("timer_init done\n");
}

