#ifndef __LIB_KERNEL_STDIO_KERNEL_H
#define __LIB_KERNEL_STDIO_KERNEL_H

/* 供内核使用的格式化输出函数（不依赖系统调用） */
void printk(const char* format, ...);

#endif
