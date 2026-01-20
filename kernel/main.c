#include "print.h"
#include "init.h"
#include "memory.h"
#include "thread.h"
#include "sched.h"
#include "interrupt.h"

void k_thread_a(void*);             // 线程A
void k_thread_b(void*);             // 线程B

int main(void) {
    put_str("I am kernel\n");
    init_all();

    void* addr = get_kernel_pages(3);
    put_str("get_kernel_page: ");
    put_hex((uint32_t)addr);
    put_char('\n');

    /* 创建测试线程 */
    thread_start("thread_a", 10, k_thread_a, "A ");  // nice=-10, weight=9548 (高优先级)
    thread_start("thread_b", 20, k_thread_b, "B ");  // nice=0, weight=1024 (普通优先级)

    /* 开启中断，允许时钟中断触发调度 */
    intr_enable();

    while(1);
    return 0;
}


/* 在线程中运行的函数 */
void k_thread_a(void* arg) {
    char* para = arg;
    while (1) {
        put_str(para);
        for(int i = 0; i < 50000; i++);  // 加大延时，让输出更慢
    }
}

void k_thread_b(void* arg) {
    char* para = arg;
    while (1) {
        put_str(para);
        for(int i = 0; i < 50000; i++);  // 加大延时，让输出更慢
    }
}