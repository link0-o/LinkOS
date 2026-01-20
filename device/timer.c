#include "timer.h"
#include "io.h"
#include "print.h"
#include "thread.h"
#include "interrupt.h"
#include "debug.h"
#include "sched.h"

#define IRQ0_FREQUENCY      100                 //每秒100次中断,10ms一次
#define INPUT_FREQUENCY     1193180
#define COUNTER0_VALUE      INPUT_FREQUENCY / IRQ0_FREQUENCY        //中断发生频率
#define COUNTER0_PORT       0x40                //计数器 0 的数据端口地址
#define COUNTER0_NO         0
#define COUNTER_MODE        2                   //工作方式2: 比率发生器
#define READ_WRITE_LATCH    3                   //读写格式为先写低 8 位，再写高 8 位(因为x86是小端序)
#define PIT_CONTROL_PORT    0x43                //控制寄存器

/* CFS 调度参数 (参考 Linux) */
#define SCHED_LATENCY_TICKS  3                  // 调度周期: 30ms (3 * 10ms)
                                                // Linux 默认 6ms,我们用 30ms 更适合 100Hz
#define SCHED_MIN_GRANULARITY 1                 // 最小粒度: 10ms (1 tick)

/* 系统运行的总tick数 */
uint32_t ticks;

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
    
    /* 初始化系统tick计数器 */
    ticks = 0;
    
    /* 设置 8253 的定时周期，也就是发中断的周期 */
    frequency_set(COUNTER0_PORT, \
                  COUNTER0_NO, \
                  READ_WRITE_LATCH,\
                  COUNTER_MODE, \
                  COUNTER0_VALUE);
    
    /* 注册时钟中断处理函数到 IDT 表 */
    idt_table[0x20] = timer_interrupt_handler;  // IRQ0 对应中断向量号 0x20
                  
    put_str("timer_init done\n");
}

/* 时钟中断处理函数 
 * 每 10ms 触发一次 (IRQ0_FREQUENCY = 100Hz)
 * 适配 CFS 调度器,参考 Linux 内核设计
 */
void timer_interrupt_handler(uint8_t vec_nr) {
    struct task_struct* cur = running_thread();
    
    /* 检查栈是否溢出 */
    ASSERT(cur->stack_magic == 0x19870916);
    
    /* 累加当前线程的运行时间 ← 这里自动增加! */
    cur->elapsed_ticks++;
    
    /* 累加系统总运行时间 */
    ticks++;
    
    /* CFS 调度: 当前线程运行一段时间后,检查是否需要重新调度
     * 参考 Linux CFS 的 sched_latency (调度延迟) 概念
     * Linux 默认 6ms,我们用 30ms (SCHED_LATENCY_TICKS = 3)
     * 这样既保证了公平性,又不会过于频繁地触发调度
     */
    uint64_t delta_exec = cur->elapsed_ticks - cur->exec_start;
    if (delta_exec >= SCHED_LATENCY_TICKS) {  // 当前线程已运行 30ms
        schedule();  // 触发调度器,让它根据 vruntime 决定是否切换
    }
}
