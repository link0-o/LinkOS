#include "print.h"
#include "init.h"
#include "memory.h"

int main(void) {
    put_str("I am kernel\n");
    init_all();
    // asm volatile("sti");  // 为演示中断处理,在此临时开中断

    void* addr = get_kernel_pages(3);
    put_str("get_kernel_page: ");
    put_hex((uint32_t)addr);
    put_char('\n');

    while(1);
    return 0;
}