#ifndef __KERNEL_INTERRUPT_H
#define __KERNEL_INTERRUPT_H
#include "stdint.h"

// 中断处理函数类型定义
typedef void (*intr_handler)(uint8_t);

// 初始化中断
void idt_init(void);

#endif
