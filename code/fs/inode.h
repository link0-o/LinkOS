#ifndef __FS_INODE_H
#define __FS_INODE_H

#include "stdint.h"
#include "global.h"
#include "list.h"
#include "ide.h"

/* ===== ext2 风格三级块索引 (仿照 Linux) =====
 *
 * i_sectors[] 数组布局 (共 15 个条目, 对应 ext2 的 i_block[15]):
 *   [0..11] : 12 个直接块, 直接指向数据块
 *   [12]    : 一级间接块(single indirect), 指向一个间接表, 含 1024 个数据块号
 *   [13]    : 二级间接块(double indirect), 指向 1024 个一级间接表
 *   [14]    : 三级间接块(triple indirect), 指向 1024 个二级间接表
 *
 * 所有块号均为 **分区相对** (partition-relative block number),
 * 而非绝对 LBA.  0 表示 "未分配" (block 0 是引导块, 永远不会被作为数据块分配).
 * I/O 时转换: absolute_LBA = part->start_lba + block_no * SECTORS_PER_BLOCK
 *
 *      inode
 *   ┌──────────┐
 *   │i_sec[ 0] │──► 数据块 0
 *   │   ...    │
 *   │i_sec[11] │──► 数据块 11                               12 blocks
 *   ├──────────┤
 *   │i_sec[12] │──► [间接表]──► 1024 个数据块               1,024 blocks
 *   ├──────────┤
 *   │i_sec[13] │──► [L1 表]                                 1024² = 1,048,576 blocks
 *   │          │       ├──► [L2 #0]──► 1024 个数据块
 *   │          │       └──► [L2#1023]──► 1024 个数据块
 *   ├──────────┤
 *   │i_sec[14] │──► [T1 表]                                 1024³ > 10 亿 blocks
 *   │          │       ├──► [T2 #0]──► [T3 #0]──► 1024 个数据块
 *   │          │       │                ...
 *   │          │       └──► [T2#1023]──► ...
 *   └──────────┘
 *
 * 容量计算 (块大小 = 4096 字节 = 页大小, 每间接表 1024 条目):
 *   直接块  : 12 × 4096                   =      49,152 B =    48 KB
 *   一级间接: 1024 × 4096                  =   4,194,304 B =     4 MB
 *   二级间接: 1024² × 4096                 = 4,294,967,296 B ≈   4 GB
 *   三级间接: 1024³ × 4096                 > 4 TB (超出 32 位寻址)
 *   ─────────────────────────────────────────────────────────────
 *   最大文件: 受限于 uint32_t i_size, 约 4 GB
 */

/* 块索引常量 */
#define INODE_DIRECT_BLOCKS     12      /* 直接块的数量 */
#define INODE_INDIRECT_ENTRIES  1024    /* 每个间接块表的条目数 (4096 / 4) */
#define INODE_SINGLE_INDIRECT   12      /* i_sectors[12] = 一级间接块表 */
#define INODE_DOUBLE_INDIRECT   13      /* i_sectors[13] = 二级间接块表 */
#define INODE_TRIPLE_INDIRECT   14      /* i_sectors[14] = 三级间接块表 */
#define INODE_SECTORS_CNT       15      /* i_sectors 数组长度 (ext2: EXT2_N_BLOCKS=15) */

/* 目录最大数据块数 (只使用直接块 + 一级间接块, 足够存储 ~170K 个目录项) */
#define DIR_MAX_BLOCKS  (INODE_DIRECT_BLOCKS + INODE_INDIRECT_ENTRIES)  /* 1036 */

/* 文件最大大小 (字节): 受限于 uint32_t i_size ≈ 4 GB
 * 三级索引理论容量 (12+1024+1024²+1024³ 块) 远超此限制 */
#define FILE_MAX_SIZE   ((uint32_t)0xFFFFFFFF)

struct inode {
    uint32_t i_no;               // inode 编号
    uint32_t i_size;             // 文件大小(字节), 目录则为所有目录项大小之和
    uint32_t i_open_cnts;        // 记录此文件被打开的次数 (运行时, 不写盘)
    bool     write_deny;         // 写文件时不能并行写 (运行时, 不写盘)

    /* 数据块索引: 存储分区相对块号 (0=未分配)
     * 0~11 直接块, 12 一级间接, 13 二级间接, 14 三级间接 */
    uint32_t i_sectors[INODE_SECTORS_CNT];
    struct list_elem inode_tag;  // 用于加入 partition 的 open_inodes 链表 (运行时)
};

/* inode 在磁盘上的位置信息, 当局部变量辅助使用 */
struct inode_position {
    bool     two_sec;   // inode 是否跨越两个扇区
    uint32_t sec_lba;   // inode 所在的绝对扇区 LBA (已转换, 可直接用于 I/O)
    uint32_t off_size;  // inode 在扇区内的字节偏移
};

/* inode 操作函数 */

/* 定位 inode 在磁盘上的位置 */
void inode_locate(struct partition* part, uint32_t inode_no, struct inode_position* inode_pos);

/* 将 inode 写回磁盘（同步），io_buf 需由调用者提供（至少 2 扇区大小） */
void inode_sync(struct partition* part, struct inode* inode, void* io_buf);

/* 根据 inode 号打开 inode：先在 open_inodes 链表中查找，不存在则从磁盘加载 */
struct inode* inode_open(struct partition* part, uint32_t inode_no);

/* 关闭 inode：减少引用计数，为 0 则从 open_inodes 链表移除并释放内存 */
void inode_close(struct inode* inode);

/* 初始化 inode（填充默认值） */
void inode_init(uint32_t inode_no, struct inode* new_inode);

/* 将 inode 对应的所有数据块和间接块回收(释放位图中的位) */
void inode_release(struct partition* part, uint32_t inode_no);

/* 清空磁盘上 inode_no 对应的 inode 数据(全部置零) */
void inode_delete(struct partition* part, uint32_t inode_no, void* io_buf);

/* ===== 多级块索引辅助函数 ===== */

/* 获取 inode 的第 block_idx 个逻辑数据块的分区相对块号
 * 返回块号, 如果该块未分配返回 0 */
uint32_t get_data_block(struct partition* part, struct inode* inode, uint32_t block_idx);

/* 为 inode 的第 block_idx 个逻辑数据块分配新块
 * 自动处理间接块表的分配
 * 返回新分配块的分区相对块号, 失败返回 0 */
uint32_t alloc_data_block(struct partition* part, struct inode* inode, uint32_t block_idx);

/* 收集 inode 的所有块地址到 all_blocks 数组 (仅限直接块+一级间接块范围, 用于目录操作)
 * all_blocks 中存储的是分区相对块号 */
void collect_dir_blocks(struct partition* part, struct inode* inode, uint32_t* all_blocks);

#endif
