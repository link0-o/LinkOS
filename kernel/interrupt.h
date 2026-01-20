#ifndef __KERNEL_INTERRUPT_H
#define __KERNEL_INTERRUPT_H
#include "stdint.h"

#define IDT_DESC_CNT 0x30  // 支持的中断数 (48 个)

// 中断处理函数类型定义
typedef void (*intr_handler)(uint8_t);

// IDT 中断处理函数表
extern intr_handler idt_table[IDT_DESC_CNT];

// 初始化中断
void idt_init(void);

enum intr_status{       //中断状态
    INTR_OFF,           //中断关闭
    INTR_ON             //中断开启
};

enum intr_status intr_get_status();                 /* 获取当前中断状态 */
enum intr_status intr_enable();                     /* 开中断并返回开中断前的状态*/
enum intr_status intr_disable();                    /* 关中断，并且返回关中断前的状态 */  
enum intr_status intr_set_status(enum intr_status status);          /* 将中断状态设置为 status */

#endif
