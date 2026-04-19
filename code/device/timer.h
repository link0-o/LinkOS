#ifndef __DEVICE_TIMER_H
#define __DEVICE_TIMER_H
#include "stdint.h"

/* 初始化定时器 */
void timer_init(void);

/* 时钟中断处理函数 */
void timer_interrupt_handler(uint8_t vec_nr);

/* 以毫秒为单位的休眠函数 */
void mtime_sleep(uint32_t m_seconds);

/* 获取系统运行的总tick数 */
extern uint32_t ticks;

#endif