#include "stdio-kernel.h"
#include "stdio.h"
#include "console.h"
#include "stdarg.h"

/* 供内核使用的格式化输出函数
 * 与 printf 原理相同，区别是直接调用 console_put_str，不走系统调用 */
void printk(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buf[1024] = {0};
    vsprintf(buf, format, args);
    va_end(args);
    console_put_str(buf);
}
