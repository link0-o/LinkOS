#ifndef __LIB_USER_STDIO_H
#define __LIB_USER_STDIO_H

#include "stdint.h"
#include "stdarg.h"

/* 支持的格式说明符：%d %u %x(小写16进制) %X(大写16进制) %o(8进制) %c %s %%
 * 修饰符：宽度（%8d）、左对齐（%-8s）、补零（%05d）*/

int vsprintf(char* buf, const char* fmt, va_list ap);       // 格式化字符串
int sprintf(char* buf, const char* fmt, ...);           // 格式化字符串
int printf(const char* fmt, ...);

#endif
