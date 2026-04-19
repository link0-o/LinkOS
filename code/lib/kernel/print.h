#ifndef __LIB_KERNEL_PRINT_H
#define __LIB_KERNEL_PRINT_H
#include "stdint.h"
void put_char(uint8_t char_asci);
void put_str(char* message);
void put_int(int32_t num);       // 打印十进制 32 位整数
void put_hex(uint32_t num);      // 打印 0x 前缀的 32 位十六进制数

/* 光标控制函数 */
void set_cursor(uint32_t cursor_pos);
uint32_t get_cursor(void);

/* 清屏 */
void cls_screen(void);

#endif