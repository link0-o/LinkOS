#ifndef DEVICE_CONSOLE_H
#define DEVICE_CONSOLE_H
#include "stdint.h"

void console_init();    // 初始化终端
void console_acquire(); // 获取终端
void console_release(); // 释放终端
void console_put_str(char* str); // 终端中输出字符串
void console_put_char(uint8_t ch); // 终端中输出字符
void console_put_int(int32_t num); // 终端中输出整数
void console_put_hex(uint32_t num); // 终端中输出十六进制整数

#endif // DEVICE_CONSOLE_H