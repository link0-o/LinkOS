#include "tss.h"
#include "stdint.h"
#include "global.h"
#include "string.h"
#include "print.h"
#include "thread.h"

#define PG_SIZE 4096

// 定义 TSS 结构体实例 (全局唯一)
static struct tss tss;

/* 更新 tss 中 esp0 字段的值为 pthread 的 0 级栈 */
void update_tss_esp(struct task_struct* pthread) {
    tss.esp0 = (uint32_t*)((uint32_t)pthread + PG_SIZE);
}

/* 创建 gdt 描述符 */
static struct gdt_desc make_gdt_desc(uint32_t* desc_addr, 
                                      uint32_t limit, 
                                      uint8_t attr_low, 
                                      uint8_t attr_high) {
    uint32_t desc_base = (uint32_t)desc_addr;
    struct gdt_desc desc;
        
    desc.limit_low_word = limit & 0x0000ffff;
    desc.base_low_word = desc_base & 0x0000ffff;
    desc.base_mid_byte = ((desc_base & 0x00ff0000) >> 16);
    desc.attr_low_byte = (uint8_t)(attr_low);
    desc.limit_high_attr_high = (((limit & 0x000f0000) >> 16) + (uint8_t)(attr_high));
    desc.base_high_byte = desc_base >> 24;
    
    return desc;
}

/* 在 gdt 中创建 tss 并重新加载 gdt */
void tss_init() {
    put_str("tss_init start\n");
    
    uint32_t tss_size = sizeof(tss);
    memset(&tss, 0, tss_size);
    tss.ss0 = SELECTOR_K_STACK;
    tss.io_base = tss_size;  // I/O 位图基址设为 TSS 末尾,表示没有 I/O 位图
    
    /* 
     * GDT 基址：
     * loader.asm 中 GDT_BASE 从 0x908 开始（前 8 字节是跳转指令 + nop 填充）
     * 虚拟地址 = 0xc0000000 + 0x908 = 0xc0000908
     * 
     * GDT[n] 地址 = 0xc0000908 + n*8
     */
    #define GDT_BASE_VADDR 0xc0000908
    
    /* 在 gdt 中添加 dpl 为 0 的 TSS 描述符 */
    // GDT[4] 地址 = 0xc0000908 + 4*8 = 0xc0000928
    *((struct gdt_desc*)(GDT_BASE_VADDR + 4*8)) = make_gdt_desc(
        (uint32_t*)&tss, 
        tss_size - 1, 
        TSS_ATTR_LOW, 
        TSS_ATTR_HIGH
    );
    
    /* 在 gdt 中添加 dpl 为 3 的用户代码段描述符 */
    // GDT[5] 地址 = 0xc0000908 + 5*8 = 0xc0000930
    *((struct gdt_desc*)(GDT_BASE_VADDR + 5*8)) = make_gdt_desc(
        (uint32_t*)0, 
        0xfffff, 
        GDT_CODE_ATTR_LOW_DPL3, 
        GDT_ATTR_HIGH
    );
    
    /* 在 gdt 中添加 dpl 为 3 的用户数据段描述符 */
    // GDT[6] 地址 = 0xc0000908 + 6*8 = 0xc0000938
    *((struct gdt_desc*)(GDT_BASE_VADDR + 6*8)) = make_gdt_desc(
        (uint32_t*)0, 
        0xfffff, 
        GDT_DATA_ATTR_LOW_DPL3, 
        GDT_ATTR_HIGH
    );
    
    /* 
     * 注意：不需要重新加载 GDT！
     * loader.asm 中的 gdt_ptr 已经设置了足够大的 limit (GDT_TOTAL_LIMIT = 511)
     * 足以容纳所有预留的描述符槽位
     * 我们只需要将新描述符写入内存,CPU 就能使用它们
     */

    /* 加载 TSS 到 TR 寄存器 */
    asm volatile ("ltr %w0" : : "r" (SELECTOR_TSS));
    
    put_str("tss_init and ltr done\n");
}
