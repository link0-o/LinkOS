#ifndef __DEVICE_IDE_H
#define __DEVICE_IDE_H

#include "stdint.h"
#include "global.h"
#include "list.h"
#include "bitmap.h"
#include "sync.h"

/* 分区结构 */
struct partition {
    uint32_t start_lba;             // 起始扇区
    uint32_t sec_cnt;               // 扇区数
    struct disk* my_disk;           // 分区所属的硬盘
    struct list_elem part_tag;      // 用于队列中的标记
    char name[8];                   // 分区名称（如 sda1、sda2）
    struct super_block* sb;         // 本分区的超级块（文件系统中使用）
    Bitmap block_bitmap;            // 块位图（管理块的使用情况）
    Bitmap inode_bitmap;            // i 结点位图
    struct list open_inodes;        // 本分区打开的 i 结点队列
};

/* 硬盘结构 */
struct disk {
    char name[8];                       // 本硬盘的名称（如 sda、sdb）
    struct ide_channel* my_channel;     // 此硬盘归属于哪个 ide 通道
    uint8_t dev_no;                     // 本硬盘是主盘（0）还是从盘（1）
    bool dma_supported;                 // IDENTIFY 返回该盘支持 ATA DMA 命令
    struct partition prim_parts[4];     // 主分区，最多 4 个
    struct partition logic_parts[8];    // 逻辑分区，最多支持 8 个
};

/* ATA 通道结构（也称 IDE 通道） */
struct ide_channel {
    char name[8];                   // 本 ATA 通道名称（如 ide0、ide1）
    uint16_t port_base;             // 本通道的命令块寄存器起始端口号
    uint8_t irq_no;                 // 本通道所用的中断向量号
    uint16_t dma_base;              // PCI Bus Master IDE 寄存器基址（0 表示不可用）
    uint32_t prdt_phys;             // PRDT 的物理地址
    void* prdt;                     // PRDT 的虚拟地址（1 页）
    bool dma_enabled;               // 当前通道是否启用了 BMIDE DMA
    struct lock lock;               // 通道锁（同一通道一次只能操作一块硬盘）
    bool expecting_intr;            // 表示等待硬盘的中断（已向硬盘发命令时置为 true）
    struct semaphore disk_done;     // 用于阻塞和唤醒驱动程序的信号量
    struct disk devices[2];         // 一个通道上连接两个硬盘，下标 0 为主盘，1 为从盘
};

/* 全局 ATA 通道数组及通道计数 */
extern uint8_t channel_cnt;             // 机器上的 ATA 通道数
extern struct ide_channel channels[2];  // 两个 ATA 通道
extern struct list partition_list;      // 所有分区的链表

/* 硬盘驱动接口 */
void ide_init(void);
void ide_read(struct disk* hd, uint32_t lba, void* buf, uint32_t sec_cnt);
void ide_write(struct disk* hd, uint32_t lba, void* buf, uint32_t sec_cnt);
void intr_hd_handler(uint8_t irq_no);

#endif
