#include "print.h"
#include "init.h"
#include "memory.h"
#include "thread.h"

void k_thread_a(void*);             // 线程A

int main(void) {
    put_str("I am kernel\n");
    init_all();
    // asm volatile("sti");  // 为演示中断处理,在此临时开中断

    void* addr = get_kernel_pages(3);
    put_str("get_kernel_page: ");
    put_hex((uint32_t)addr);
    put_char('\n');

    thread_start("k_thread_a", 31, k_thread_a, "argA ");

    while(1);
    return 0;
}


/* 在线程中运行的函数 */
void k_thread_a(void* arg) {
    char* para = arg;
    while (1) {
        put_str(para);
    }
}