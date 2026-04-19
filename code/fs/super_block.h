#ifndef __FS_SUPER_BLOCK_H
#define __FS_SUPER_BLOCK_H

#include "stdint.h"

/* 超级块结构（占 512 字节, 存储在块 0 的第 1 个扇区）
 *
 * 仿照 Linux ext2 设计, 所有地址均为 **分区相对块号**, 不使用绝对 LBA,
 * 保证文件系统可重定位 (分区起始 LBA 改变时无需修改任何磁盘数据).
 * 块大小 = 4096 字节 = 8 个扇区 = 1 页.
 *
 * 分区磁盘布局（以块为单位）：
 * ┌───────────────┬────────────┬─────────────┬────────────┬─────────────┐
 * │  块 0 (引导+SB)│  块位图    │ inode 位图  │ inode 表   │  数据区     │
 * │ sec0=boot     │ (块 1起)   │  ...        │ ...        │ (块 N 起)   │
 * │ sec1=超级块   │            │             │            │             │
 * └───────────────┴────────────┴─────────────┴────────────┴─────────────┘
 *
 * 转换公式:  绝对LBA = part->start_lba + block_no * SECTORS_PER_BLOCK
 * 超级块固定在:  part->start_lba + 1  (扇区级别, 不经过块转换)
 */
struct super_block {
    uint32_t magic;              // 文件系统魔数，用于识别是否已格式化
    uint32_t sec_cnt;            // 本分区总扇区数
    uint32_t inode_cnt;          // 本分区 inode 总数
    uint32_t block_size;         // 块大小 (字节), 固定为 4096 (= 页大小)

    uint32_t block_bitmap_start; // 块位图起始块号 (分区相对)
    uint32_t block_bitmap_blks;  // 块位图占用的块数

    uint32_t inode_bitmap_start; // inode 位图起始块号 (分区相对)
    uint32_t inode_bitmap_blks;  // inode 位图占用的块数

    uint32_t inode_table_start;  // inode 表起始块号 (分区相对)
    uint32_t inode_table_blks;   // inode 表占用的块数

    uint32_t data_start;         // 数据区起始块号 (分区相对)
    uint32_t root_inode_no;      // 根目录所在的 inode 号
    uint32_t dir_entry_size;     // 目录项的结构占据大小（字节）

    uint8_t  pad[460];           // 填充至 512 字节（13 * 4 = 52, 512 - 52 = 460）
} __attribute__((packed));

#endif
