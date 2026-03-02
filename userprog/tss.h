#ifndef USERGROG_TSS_H
#define USERGROG_TSS_H
#include "stdint.h"

// 前向声明
struct task_struct;

// Task State Segment (TSS) 结构体定义
// 完整的 32 位 TSS 结构,符合 Intel 手册规范
struct tss {
    uint32_t backlink;      // 上一个 TSS 的段选择子 (链接字段)
    uint32_t* esp0;         // Ring 0 栈指针 (内核栈)
    uint32_t ss0;           // Ring 0 栈段选择子
    uint32_t* esp1;         // Ring 1 栈指针 (很少用)
    uint32_t ss1;           // Ring 1 栈段选择子
    uint32_t* esp2;         // Ring 2 栈指针 (很少用)
    uint32_t ss2;           // Ring 2 栈段选择子
    uint32_t cr3;           // 页目录基址
    uint32_t (*eip)(void);  // 指令指针 (程序计数器)
    uint32_t eflags;        // 标志寄存器
    uint32_t eax;       
    uint32_t ecx;        
    uint32_t edx;       
    uint32_t ebx;        
    uint32_t esp;           // 栈指针
    uint32_t ebp;           // 基址指针
    uint32_t esi;           // 源索引
    uint32_t edi;           // 目的索引
    uint32_t es;            // ES 段寄存器
    uint32_t cs;            // CS 段寄存器
    uint32_t ss;            // SS 段寄存器
    uint32_t ds;            // DS 段寄存器
    uint32_t fs;            // FS 段寄存器
    uint32_t gs;            // GS 段寄存器
    uint32_t ldt;           // LDT 段选择子
    uint32_t trace;         // 调试用 T 位
    uint32_t io_base;       // I/O 位图基址 (相对 TSS 起始的偏移)
};  // 总大小: 104 字节


void tss_init(void);
void update_tss_esp(struct task_struct* pthread);

// TSS 选择子 (对应 GDT[4],索引=4)
// 计算: (4 << 3) + (TI_GDT << 2) + RPL0 = 0x20
#define TSS_SELECTOR 0x20

#endif