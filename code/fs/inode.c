#include "inode.h"
#include "ide.h"
#include "global.h"
#include "debug.h"
#include "string.h"
#include "list.h"
#include "interrupt.h"
#include "super_block.h"
#include "file.h"
#include "fs.h"

extern void* sys_malloc(uint32_t size);
extern void  sys_free(void* ptr);

/* 定位 inode_no 在磁盘上的块位置和偏移 */
void inode_locate(struct partition* part, uint32_t inode_no,
                  struct inode_position* inode_pos) {
    ASSERT(inode_no < MAX_FILES_PER_PART);
    uint32_t inode_table_blk = part->sb->inode_table_start;  /* 分区相对块号 */
    uint32_t inode_size = sizeof(struct inode);
    uint32_t off_size = inode_no * inode_size;
    uint32_t off_blk  = off_size / BLOCK_SIZE;
    uint32_t off_size_in_blk = off_size % BLOCK_SIZE;

    uint32_t left_in_blk = BLOCK_SIZE - off_size_in_blk;
    if (left_in_blk < inode_size) {
        inode_pos->two_sec = true;
    } else {
        inode_pos->two_sec = false;
    }
    /* 转换为绝对 LBA, 可直接用于 I/O */
    inode_pos->sec_lba  = blk2lba(part, inode_table_blk + off_blk);
    inode_pos->off_size = off_size_in_blk;
}

/* 将内存中的 inode 写回磁盘
 * io_buf: 调用者提供的缓冲区，大小至少 2 个块 (8192 字节) */
void inode_sync(struct partition* part, struct inode* inode, void* io_buf) {
    uint8_t inode_no = inode->i_no;
    struct inode_position inode_pos;
    inode_locate(part, inode_no, &inode_pos);
    ASSERT(inode_pos.sec_lba <= (part->start_lba + part->sec_cnt));

    struct inode pure_inode;
    memcpy(&pure_inode, inode, sizeof(struct inode));
    pure_inode.i_open_cnts = 0;
    pure_inode.write_deny  = false;
    pure_inode.inode_tag.prev = NULL;
    pure_inode.inode_tag.next = NULL;

    char* inode_buf = (char*)io_buf;
    if (inode_pos.two_sec) {
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2 * SECTORS_PER_BLOCK);
        memcpy(inode_buf + inode_pos.off_size, &pure_inode, sizeof(struct inode));
        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 2 * SECTORS_PER_BLOCK);
    } else {
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, SECTORS_PER_BLOCK);
        memcpy(inode_buf + inode_pos.off_size, &pure_inode, sizeof(struct inode));
        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, SECTORS_PER_BLOCK);
    }
}

/* 根据 inode 号打开 inode */
struct inode* inode_open(struct partition* part, uint32_t inode_no) {
    struct list_elem* elem = part->open_inodes.head.next;
    struct inode* inode_found;
    while (elem != &part->open_inodes.tail) {
        inode_found = elem2entry(struct inode, inode_tag, elem);
        if (inode_found->i_no == inode_no) {
            inode_found->i_open_cnts++;
            return inode_found;
        }
        elem = elem->next;
    }

    struct inode_position inode_pos;
    inode_locate(part, inode_no, &inode_pos);

    struct task_struct* cur = running_thread();
    uint32_t* cur_pagedir_bak = cur->pgdir;
    cur->pgdir = NULL;
    inode_found = (struct inode*)sys_malloc(sizeof(struct inode));
    cur->pgdir = cur_pagedir_bak;

    char* inode_buf;
    if (inode_pos.two_sec) {
        inode_buf = (char*)sys_malloc(BLOCK_SIZE * 2);
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2 * SECTORS_PER_BLOCK);
    } else {
        inode_buf = (char*)sys_malloc(BLOCK_SIZE);
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, SECTORS_PER_BLOCK);
    }
    memcpy(inode_found, inode_buf + inode_pos.off_size, sizeof(struct inode));

    list_push(&part->open_inodes, &inode_found->inode_tag);
    inode_found->i_open_cnts = 1;

    sys_free(inode_buf);
    return inode_found;
}

/* 关闭 inode */
void inode_close(struct inode* inode) {
    enum intr_status old_status = intr_disable();
    if (--inode->i_open_cnts == 0) {
        list_remove(&inode->inode_tag);
        struct task_struct* cur = running_thread();
        uint32_t* cur_pagedir_bak = cur->pgdir;
        cur->pgdir = NULL;
        sys_free(inode);
        cur->pgdir = cur_pagedir_bak;
    }
    intr_set_status(old_status);
}

/* 初始化 inode */
void inode_init(uint32_t inode_no, struct inode* new_inode) {
    new_inode->i_no = inode_no;
    new_inode->i_size = 0;
    new_inode->i_open_cnts = 0;
    new_inode->write_deny = false;
    uint8_t sec_idx = 0;
    while (sec_idx < INODE_SECTORS_CNT) {
        new_inode->i_sectors[sec_idx] = 0;
        sec_idx++;
    }
}

/* 清空磁盘上的 inode（全部置零） */
void inode_delete(struct partition* part, uint32_t inode_no, void* io_buf) {
    ASSERT(inode_no < MAX_FILES_PER_PART);
    struct inode_position inode_pos;
    inode_locate(part, inode_no, &inode_pos);
    ASSERT(inode_pos.sec_lba <= (part->start_lba + part->sec_cnt));

    char* inode_buf = (char*)io_buf;
    if (inode_pos.two_sec) {
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2 * SECTORS_PER_BLOCK);
        memset(inode_buf + inode_pos.off_size, 0, sizeof(struct inode));
        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 2 * SECTORS_PER_BLOCK);
    } else {
        ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, SECTORS_PER_BLOCK);
        memset(inode_buf + inode_pos.off_size, 0, sizeof(struct inode));
        ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, SECTORS_PER_BLOCK);
    }
}

/* 释放一个分区相对块号对应的数据块 (清除 block_bitmap 中的位) */
static void free_block(struct partition* part, uint32_t block_no) {
    uint32_t bitmap_idx = blk2bidx(part, block_no);
    ASSERT(bitmap_idx > 0);
    bitmap_set(&part->block_bitmap, bitmap_idx, 0);
    bitmap_sync(part, bitmap_idx, BLOCK_BITMAP);
}

/* 回收 inode 占用的所有数据块
 * 支持直接块 + 一级间接块 + 二级间接块 + 三级间接块
 * 使用堆分配缓冲区避免栈溢出 (每个间接表 = 4KB) */
void inode_release(struct partition* part, uint32_t inode_no) {
    struct inode* inode_to_del = inode_open(part, inode_no);
    ASSERT(inode_to_del->i_no == inode_no);

    uint32_t block_idx;

    /* 预分配间接表缓冲区 (最多同时需要 3 个, 用于三级间接遍历) */
    uint32_t* tbl1 = (uint32_t*)sys_malloc(BLOCK_SIZE);
    uint32_t* tbl2 = (uint32_t*)sys_malloc(BLOCK_SIZE);
    uint32_t* tbl3 = (uint32_t*)sys_malloc(BLOCK_SIZE);

    /* 1. 回收所有直接块 */
    for (block_idx = 0; block_idx < INODE_DIRECT_BLOCKS; block_idx++) {
        if (inode_to_del->i_sectors[block_idx] != 0) {
            free_block(part, inode_to_del->i_sectors[block_idx]);
        }
    }

    /* 2. 回收一级间接块 */
    if (inode_to_del->i_sectors[INODE_SINGLE_INDIRECT] != 0) {
        ide_read(part->my_disk, blk2lba(part, inode_to_del->i_sectors[INODE_SINGLE_INDIRECT]),
                 tbl1, SECTORS_PER_BLOCK);
        uint32_t idx;
        for (idx = 0; idx < INODE_INDIRECT_ENTRIES; idx++) {
            if (tbl1[idx] != 0) free_block(part, tbl1[idx]);
        }
        free_block(part, inode_to_del->i_sectors[INODE_SINGLE_INDIRECT]);
    }

    /* 3. 回收二级间接块: L1 → L2[] → data[] */
    if (inode_to_del->i_sectors[INODE_DOUBLE_INDIRECT] != 0) {
        ide_read(part->my_disk, blk2lba(part, inode_to_del->i_sectors[INODE_DOUBLE_INDIRECT]),
                 tbl1, SECTORS_PER_BLOCK);
        uint32_t l1_idx;
        for (l1_idx = 0; l1_idx < INODE_INDIRECT_ENTRIES; l1_idx++) {
            if (tbl1[l1_idx] != 0) {
                ide_read(part->my_disk, blk2lba(part, tbl1[l1_idx]),
                         tbl2, SECTORS_PER_BLOCK);
                uint32_t l2_idx;
                for (l2_idx = 0; l2_idx < INODE_INDIRECT_ENTRIES; l2_idx++) {
                    if (tbl2[l2_idx] != 0) free_block(part, tbl2[l2_idx]);
                }
                free_block(part, tbl1[l1_idx]);
            }
        }
        free_block(part, inode_to_del->i_sectors[INODE_DOUBLE_INDIRECT]);
    }

    /* 4. 回收三级间接块: T1 → T2[] → T3[] → data[] */
    if (inode_to_del->i_sectors[INODE_TRIPLE_INDIRECT] != 0) {
        ide_read(part->my_disk, blk2lba(part, inode_to_del->i_sectors[INODE_TRIPLE_INDIRECT]),
                 tbl1, SECTORS_PER_BLOCK);
        uint32_t t1_idx;
        for (t1_idx = 0; t1_idx < INODE_INDIRECT_ENTRIES; t1_idx++) {
            if (tbl1[t1_idx] != 0) {
                ide_read(part->my_disk, blk2lba(part, tbl1[t1_idx]),
                         tbl2, SECTORS_PER_BLOCK);
                uint32_t t2_idx;
                for (t2_idx = 0; t2_idx < INODE_INDIRECT_ENTRIES; t2_idx++) {
                    if (tbl2[t2_idx] != 0) {
                        ide_read(part->my_disk, blk2lba(part, tbl2[t2_idx]),
                                 tbl3, SECTORS_PER_BLOCK);
                        uint32_t t3_idx;
                        for (t3_idx = 0; t3_idx < INODE_INDIRECT_ENTRIES; t3_idx++) {
                            if (tbl3[t3_idx] != 0) free_block(part, tbl3[t3_idx]);
                        }
                        free_block(part, tbl2[t2_idx]);
                    }
                }
                free_block(part, tbl1[t1_idx]);
            }
        }
        free_block(part, inode_to_del->i_sectors[INODE_TRIPLE_INDIRECT]);
    }

    sys_free(tbl3);
    sys_free(tbl2);
    sys_free(tbl1);

    /* 5. 释放 inode 位图中的位 */
    bitmap_set(&part->inode_bitmap, inode_no, 0);
    bitmap_sync(part, inode_no, INODE_BITMAP);

    /* 6. 清空磁盘上 inode 数据 */
    void* io_buf = sys_malloc(BLOCK_SIZE * 2);
    inode_delete(part, inode_no, io_buf);
    sys_free(io_buf);

    inode_close(inode_to_del);
}

/* =============== 多级块索引辅助函数 =============== */

/* 逻辑块号范围常量 */
#define SINGLE_INDIRECT_START   (INODE_DIRECT_BLOCKS)                     /* 12 */
#define DOUBLE_INDIRECT_START   (SINGLE_INDIRECT_START + INODE_INDIRECT_ENTRIES) /* 1036 */
#define TRIPLE_INDIRECT_START   (DOUBLE_INDIRECT_START + \
                                 INODE_INDIRECT_ENTRIES * INODE_INDIRECT_ENTRIES) /* 1049612 */

/* 静态缓冲区: get_data_block 专用 (非抢占内核安全, 避免频繁堆分配) */
static uint32_t _get_data_tbl[INODE_INDIRECT_ENTRIES];

/* 获取 inode 的第 block_idx 个逻辑数据块的分区相对块号
 *
 *  block_idx 范围                              对应位置
 *  ───────────────────────────────────────────────────────
 *  [0, 12)                                    直接块
 *  [12, 1036)                                 一级间接
 *  [1036, 1049612)                            二级间接
 *  [1049612, 1049612 + 1024³)                 三级间接
 *
 * 返回 0 表示该块未分配 */
uint32_t get_data_block(struct partition* part, struct inode* inode, uint32_t block_idx) {
    /* 直接块 */
    if (block_idx < SINGLE_INDIRECT_START) {
        return inode->i_sectors[block_idx];
    }

    /* 一级间接块 */
    if (block_idx < DOUBLE_INDIRECT_START) {
        if (inode->i_sectors[INODE_SINGLE_INDIRECT] == 0) return 0;
        ide_read(part->my_disk, blk2lba(part, inode->i_sectors[INODE_SINGLE_INDIRECT]),
                 _get_data_tbl, SECTORS_PER_BLOCK);
        return _get_data_tbl[block_idx - SINGLE_INDIRECT_START];
    }

    /* 二级间接块 */
    if (block_idx < TRIPLE_INDIRECT_START) {
        if (inode->i_sectors[INODE_DOUBLE_INDIRECT] == 0) return 0;
        uint32_t off = block_idx - DOUBLE_INDIRECT_START;
        uint32_t l1_idx = off / INODE_INDIRECT_ENTRIES;
        uint32_t l2_idx = off % INODE_INDIRECT_ENTRIES;

        ide_read(part->my_disk, blk2lba(part, inode->i_sectors[INODE_DOUBLE_INDIRECT]),
                 _get_data_tbl, SECTORS_PER_BLOCK);
        if (_get_data_tbl[l1_idx] == 0) return 0;
        uint32_t l2_blk = _get_data_tbl[l1_idx];
        ide_read(part->my_disk, blk2lba(part, l2_blk), _get_data_tbl, SECTORS_PER_BLOCK);
        return _get_data_tbl[l2_idx];
    }

    /* 三级间接块 */
    if (inode->i_sectors[INODE_TRIPLE_INDIRECT] == 0) return 0;
    uint32_t off = block_idx - TRIPLE_INDIRECT_START;
    uint32_t t1_idx = off / (INODE_INDIRECT_ENTRIES * INODE_INDIRECT_ENTRIES);
    uint32_t rem    = off % (INODE_INDIRECT_ENTRIES * INODE_INDIRECT_ENTRIES);
    uint32_t t2_idx = rem / INODE_INDIRECT_ENTRIES;
    uint32_t t3_idx = rem % INODE_INDIRECT_ENTRIES;

    ide_read(part->my_disk, blk2lba(part, inode->i_sectors[INODE_TRIPLE_INDIRECT]),
             _get_data_tbl, SECTORS_PER_BLOCK);
    if (_get_data_tbl[t1_idx] == 0) return 0;
    uint32_t t2_blk = _get_data_tbl[t1_idx];
    ide_read(part->my_disk, blk2lba(part, t2_blk), _get_data_tbl, SECTORS_PER_BLOCK);
    if (_get_data_tbl[t2_idx] == 0) return 0;
    uint32_t t3_blk = _get_data_tbl[t2_idx];
    ide_read(part->my_disk, blk2lba(part, t3_blk), _get_data_tbl, SECTORS_PER_BLOCK);
    return _get_data_tbl[t3_idx];
}

/* 分配一个间接块表, 初始化为全 0, 返回分区相对块号, 失败返回 0 */
static uint32_t alloc_indirect_table(struct partition* part) {
    int32_t blk = block_bitmap_alloc(part);
    if (blk == -1) return 0;
    bitmap_sync(part, blk2bidx(part, blk), BLOCK_BITMAP);
    /* 初始化为全 0: 使用堆分配避免栈溢出 */
    void* zeros = sys_malloc(BLOCK_SIZE);
    if (zeros == NULL) return 0;
    memset(zeros, 0, BLOCK_SIZE);
    ide_write(part->my_disk, blk2lba(part, blk), zeros, SECTORS_PER_BLOCK);
    sys_free(zeros);
    return blk;
}

/* 为 inode 的第 block_idx 个逻辑数据块分配新块
 * 返回分区相对块号, 失败返回 0 */
uint32_t alloc_data_block(struct partition* part, struct inode* inode, uint32_t block_idx) {
    /* 使用堆分配间接表缓冲区 (4KB, 不能放栈上) */
    uint32_t* tbl = (uint32_t*)sys_malloc(BLOCK_SIZE);
    if (tbl == NULL) return 0;

    /* 1. 分配数据块 */
    int32_t new_blk = block_bitmap_alloc(part);
    if (new_blk == -1) { sys_free(tbl); return 0; }
    bitmap_sync(part, blk2bidx(part, new_blk), BLOCK_BITMAP);

    /* === 直接块 === */
    if (block_idx < SINGLE_INDIRECT_START) {
        inode->i_sectors[block_idx] = new_blk;
        sys_free(tbl);
        return new_blk;
    }

    /* === 一级间接块 === */
    if (block_idx < DOUBLE_INDIRECT_START) {
        if (inode->i_sectors[INODE_SINGLE_INDIRECT] == 0) {
            uint32_t t = alloc_indirect_table(part);
            if (t == 0) goto rollback_data;
            inode->i_sectors[INODE_SINGLE_INDIRECT] = t;
        }
        ide_read(part->my_disk, blk2lba(part, inode->i_sectors[INODE_SINGLE_INDIRECT]),
                 tbl, SECTORS_PER_BLOCK);
        tbl[block_idx - SINGLE_INDIRECT_START] = new_blk;
        ide_write(part->my_disk, blk2lba(part, inode->i_sectors[INODE_SINGLE_INDIRECT]),
                  tbl, SECTORS_PER_BLOCK);
        sys_free(tbl);
        return new_blk;
    }

    /* === 二级间接块 === */
    if (block_idx < TRIPLE_INDIRECT_START) {
        uint32_t off = block_idx - DOUBLE_INDIRECT_START;
        uint32_t l1_idx = off / INODE_INDIRECT_ENTRIES;
        uint32_t l2_idx = off % INODE_INDIRECT_ENTRIES;

        if (inode->i_sectors[INODE_DOUBLE_INDIRECT] == 0) {
            uint32_t t = alloc_indirect_table(part);
            if (t == 0) goto rollback_data;
            inode->i_sectors[INODE_DOUBLE_INDIRECT] = t;
        }
        ide_read(part->my_disk, blk2lba(part, inode->i_sectors[INODE_DOUBLE_INDIRECT]),
                 tbl, SECTORS_PER_BLOCK);
        if (tbl[l1_idx] == 0) {
            uint32_t t = alloc_indirect_table(part);
            if (t == 0) goto rollback_data;
            tbl[l1_idx] = t;
            ide_write(part->my_disk, blk2lba(part, inode->i_sectors[INODE_DOUBLE_INDIRECT]),
                      tbl, SECTORS_PER_BLOCK);
        }
        uint32_t l2_blk = tbl[l1_idx];
        ide_read(part->my_disk, blk2lba(part, l2_blk), tbl, SECTORS_PER_BLOCK);
        tbl[l2_idx] = new_blk;
        ide_write(part->my_disk, blk2lba(part, l2_blk), tbl, SECTORS_PER_BLOCK);
        sys_free(tbl);
        return new_blk;
    }

    /* === 三级间接块 === */
    {
        uint32_t off = block_idx - TRIPLE_INDIRECT_START;
        uint32_t t1_idx = off / (INODE_INDIRECT_ENTRIES * INODE_INDIRECT_ENTRIES);
        uint32_t rem    = off % (INODE_INDIRECT_ENTRIES * INODE_INDIRECT_ENTRIES);
        uint32_t t2_idx = rem / INODE_INDIRECT_ENTRIES;
        uint32_t t3_idx = rem % INODE_INDIRECT_ENTRIES;

        if (inode->i_sectors[INODE_TRIPLE_INDIRECT] == 0) {
            uint32_t t = alloc_indirect_table(part);
            if (t == 0) goto rollback_data;
            inode->i_sectors[INODE_TRIPLE_INDIRECT] = t;
        }
        /* 读 T1 表 */
        ide_read(part->my_disk, blk2lba(part, inode->i_sectors[INODE_TRIPLE_INDIRECT]),
                 tbl, SECTORS_PER_BLOCK);
        if (tbl[t1_idx] == 0) {
            uint32_t t = alloc_indirect_table(part);
            if (t == 0) goto rollback_data;
            tbl[t1_idx] = t;
            ide_write(part->my_disk, blk2lba(part, inode->i_sectors[INODE_TRIPLE_INDIRECT]),
                      tbl, SECTORS_PER_BLOCK);
        }
        uint32_t t2_blk = tbl[t1_idx];
        /* 读 T2 表 */
        ide_read(part->my_disk, blk2lba(part, t2_blk), tbl, SECTORS_PER_BLOCK);
        if (tbl[t2_idx] == 0) {
            uint32_t t = alloc_indirect_table(part);
            if (t == 0) goto rollback_data;
            tbl[t2_idx] = t;
            ide_write(part->my_disk, blk2lba(part, t2_blk), tbl, SECTORS_PER_BLOCK);
        }
        uint32_t t3_blk = tbl[t2_idx];
        /* 读 T3 表, 写入数据块号 */
        ide_read(part->my_disk, blk2lba(part, t3_blk), tbl, SECTORS_PER_BLOCK);
        tbl[t3_idx] = new_blk;
        ide_write(part->my_disk, blk2lba(part, t3_blk), tbl, SECTORS_PER_BLOCK);
        sys_free(tbl);
        return new_blk;
    }

rollback_data:
    bitmap_set(&part->block_bitmap, blk2bidx(part, new_blk), 0);
    bitmap_sync(part, blk2bidx(part, new_blk), BLOCK_BITMAP);
    sys_free(tbl);
    return 0;
}

/* 收集 inode 的所有块号到 all_blocks 数组 (仅直接块+一级间接块, 用于目录操作)
 * all_blocks[] 中存储的是分区相对块号 */
void collect_dir_blocks(struct partition* part, struct inode* inode, uint32_t* all_blocks) {
    memset(all_blocks, 0, DIR_MAX_BLOCKS * sizeof(uint32_t));
    uint32_t idx = 0;
    while (idx < INODE_DIRECT_BLOCKS) {
        all_blocks[idx] = inode->i_sectors[idx];
        idx++;
    }
    if (inode->i_sectors[INODE_SINGLE_INDIRECT] != 0) {
        ide_read(part->my_disk, blk2lba(part, inode->i_sectors[INODE_SINGLE_INDIRECT]),
                 all_blocks + INODE_DIRECT_BLOCKS, SECTORS_PER_BLOCK);
    }
}
