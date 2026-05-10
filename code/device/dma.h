#ifndef __DEVICE_DMA_H
#define __DEVICE_DMA_H

#include "stdint.h"
#include "global.h"

/* 8237A 模式位里的方向语义和调用侧的语义相反：
 * DMA_IO_TO_MEMORY 表示外设把数据写到内存；
 * DMA_MEMORY_TO_IO 表示 DMA 从内存读数据再送给外设。 */
enum dma_transfer_direction {
    DMA_IO_TO_MEMORY,
    DMA_MEMORY_TO_IO
};

/* 初始化 8237A 控制器。
 * 当前只做安全初始化：复位地址/计数 flip-flop，并屏蔽所有可编程通道。
 * 注意：这是 ISA DMA 控制器，不会自动接管现有 IDE PIO 文件读写路径。 */
void dma_init(void);

/* 为某个 DMA 通道写入地址、传输长度和方向。
 * 返回 false 表示缓冲区不满足 8237A 约束，例如：
 * 1. 虚拟地址对应的物理页不连续
 * 2. 越过 64KB/128KB 边界
 * 3. 超出 ISA DMA 可访问的前 16MB 物理内存 */
bool dma_channel_setup(uint8_t channel,
                       void* addr,
                       uint32_t size,
                       enum dma_transfer_direction direction);

/* 手动屏蔽/放开 DMA 通道，供具体设备驱动在启动或收尾阶段调用。 */
void dma_channel_mask(uint8_t channel);
void dma_channel_unmask(uint8_t channel);

/* 只做兼容性检查，不写硬件寄存器。 */
bool dma_buffer_is_compatible(uint8_t channel, void* addr, uint32_t size);

#endif