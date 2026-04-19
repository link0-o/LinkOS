#include "dir.h"
#include "inode.h"
#include "file.h"
#include "fs.h"
#include "super_block.h"
#include "ide.h"
#include "global.h"
#include "debug.h"
#include "string.h"
#include "interrupt.h"
#include "stdio-kernel.h"

extern void* sys_malloc(uint32_t size);
extern void  sys_free(void* ptr);

struct dir root_dir;    // 根目录

/* 打开根目录 */
void open_root_dir(struct partition* part) {
    root_dir.inode = inode_open(part, part->sb->root_inode_no);
    root_dir.dir_pos = 0;
}

/* 打开 inode_no 对应的目录 */
struct dir* dir_open(struct partition* part, uint32_t inode_no) {
    struct dir* pdir = (struct dir*)sys_malloc(sizeof(struct dir));
    pdir->inode = inode_open(part, inode_no);
    pdir->dir_pos = 0;
    return pdir;
}

/* 关闭目录（根目录不能关闭/释放） */
void dir_close(struct dir* dir) {
    if (dir == &root_dir) {
        return;
    }
    inode_close(dir->inode);
    sys_free(dir);
}

/* 用前面参数填充一个目录项结构体 */
void create_dir_entry(char* filename, uint32_t inode_no,
                      uint8_t file_type, struct dir_entry* p_de) {
    ASSERT(strlen(filename) <= MAX_FILE_NAME_LEN);
    memcpy(p_de->filename, filename, strlen(filename));
    p_de->filename[strlen(filename)] = '\0';
    p_de->i_no = inode_no;
    p_de->f_type = file_type;
}

/* 在目录 pdir 中搜索 name，找到则填充 dir_e 并返回 true */
bool search_dir_entry(struct partition* part, struct dir* pdir,
                      const char* name, struct dir_entry* dir_e) {
    uint32_t block_cnt = DIR_MAX_BLOCKS;
    uint32_t* all_blocks = (uint32_t*)sys_malloc(block_cnt * sizeof(uint32_t));
    if (all_blocks == NULL) {
        printk("search_dir_entry: sys_malloc for all_blocks failed\n");
        return false;
    }

    collect_dir_blocks(part, pdir->inode, all_blocks);  // 收集目录 inode 的所有块号 (分区相对)
    uint32_t block_idx = 0;

    /* 此缓冲区用来存储目录的块数据 */
    uint8_t* buf = (uint8_t*)sys_malloc(BLOCK_SIZE);
    struct dir_entry* p_de = (struct dir_entry*)buf;
    uint32_t dir_entry_size = part->sb->dir_entry_size;
    uint32_t dir_entry_cnt = BLOCK_SIZE / dir_entry_size;  // 每块目录项数

    /* 遍历所有块 */
    while (block_idx < block_cnt) {
        if (all_blocks[block_idx] == 0) {
            block_idx++;
            continue;
        }
        ide_read(part->my_disk, blk2lba(part, all_blocks[block_idx]), buf, SECTORS_PER_BLOCK);      // 读入目录块

        uint32_t dir_entry_idx = 0;
        while (dir_entry_idx < dir_entry_cnt) {
            if (!strcmp(p_de->filename, name)) {
                memcpy(dir_e, p_de, dir_entry_size);
                sys_free(buf);
                sys_free(all_blocks);
                return true;
            }
            dir_entry_idx++;
            p_de++;
        }
        block_idx++;
        p_de = (struct dir_entry*)buf;  // 重置指针
    }
    sys_free(buf);
    sys_free(all_blocks);
    return false;
}

/* 将目录项 p_de 写入目录 parent_dir
 * io_buf 由调用者提供，至少 2 个块大小 */
bool sync_dir_entry(struct dir* parent_dir, struct dir_entry* p_de, void* io_buf) {
    struct inode* dir_inode = parent_dir->inode;
    uint32_t dir_size = dir_inode->i_size;
    uint32_t dir_entry_size = cur_part->sb->dir_entry_size;

    ASSERT(dir_size % dir_entry_size == 0);

    uint32_t dir_entrys_per_sec = BLOCK_SIZE / dir_entry_size;
    int32_t block_no = -1;   /* 分区相对块号 */

    uint32_t block_cnt = DIR_MAX_BLOCKS;
    uint32_t* all_blocks = (uint32_t*)sys_malloc(block_cnt * sizeof(uint32_t));
    if (all_blocks == NULL) {
        printk("sync_dir_entry: sys_malloc for all_blocks failed\n");
        return false;
    }
    uint32_t block_idx = 0;

    /* 收集目录 inode 的所有块号 (分区相对) */
    collect_dir_blocks(cur_part, dir_inode, all_blocks);

    struct dir_entry* dir_e = (struct dir_entry*)io_buf;

    /* 遍历所有块，找有空闲目录项的块 */
    while (block_idx < block_cnt) {
        if (all_blocks[block_idx] == 0) {
            /* 该块未分配，需要分配新块 */
            block_no = block_bitmap_alloc(cur_part);
            if (block_no == -1) {
                printk("alloc block bitmap for sync_dir_entry failed\n");
                sys_free(all_blocks);
                return false;
            }
            /* 同步块位图 */
            bitmap_sync(cur_part, blk2bidx(cur_part, block_no), BLOCK_BITMAP);

            /* 将新块号写入 inode */
            if (block_idx < INODE_DIRECT_BLOCKS) {
                dir_inode->i_sectors[block_idx] = block_no;
            } else if (block_idx == INODE_DIRECT_BLOCKS) {
                /* 需要分配间接块表 */
                dir_inode->i_sectors[INODE_SINGLE_INDIRECT] = block_no;
                /* 再分配一个块作为实际数据块 */
                block_no = block_bitmap_alloc(cur_part);
                if (block_no == -1) {
                    /* 回滚间接块表 */
                    uint32_t rollback = blk2bidx(cur_part, dir_inode->i_sectors[INODE_SINGLE_INDIRECT]);
                    bitmap_set(&cur_part->block_bitmap, rollback, 0);
                    dir_inode->i_sectors[INODE_SINGLE_INDIRECT] = 0;
                    printk("alloc block bitmap for sync_dir_entry failed\n");
                    sys_free(all_blocks);
                    return false;
                }
                bitmap_sync(cur_part, blk2bidx(cur_part, block_no), BLOCK_BITMAP);
                all_blocks[INODE_DIRECT_BLOCKS] = block_no;
                /* 将间接块表写盘 */
                ide_write(cur_part->my_disk, blk2lba(cur_part, dir_inode->i_sectors[INODE_SINGLE_INDIRECT]),
                          all_blocks + INODE_DIRECT_BLOCKS, SECTORS_PER_BLOCK);
            } else {
                all_blocks[block_idx] = block_no;
                /* 更新间接块表 */
                ide_write(cur_part->my_disk, blk2lba(cur_part, dir_inode->i_sectors[INODE_SINGLE_INDIRECT]),
                          all_blocks + INODE_DIRECT_BLOCKS, SECTORS_PER_BLOCK);
            }

            /* 新块初始化为 0 */
            memset(io_buf, 0, BLOCK_SIZE);
            memcpy(io_buf, p_de, dir_entry_size);
            ide_write(cur_part->my_disk, blk2lba(cur_part, block_no), io_buf, SECTORS_PER_BLOCK);

            dir_inode->i_size += dir_entry_size;
            sys_free(all_blocks);
            return true;
        }

        /* 块已分配，遍历其中的目录项看是否有空槽 */
        ide_read(cur_part->my_disk, blk2lba(cur_part, all_blocks[block_idx]), io_buf, SECTORS_PER_BLOCK);
        uint32_t dir_entry_idx = 0;
        while (dir_entry_idx < dir_entrys_per_sec) {
            if ((dir_e + dir_entry_idx)->f_type == FT_UNKNOWN) {
                /* 找到空闲目录项位置 */
                memcpy(dir_e + dir_entry_idx, p_de, dir_entry_size);
                ide_write(cur_part->my_disk, blk2lba(cur_part, all_blocks[block_idx]), io_buf, SECTORS_PER_BLOCK);
                dir_inode->i_size += dir_entry_size;
                sys_free(all_blocks);
                return true;
            }
            dir_entry_idx++;
        }
        block_idx++;
    }
    printk("directory is full!\n");
    sys_free(all_blocks);
    return false;
}

/* 从目录 pdir 中删除 inode_no 对应的目录项 */
bool delete_dir_entry(struct partition* part, struct dir* pdir,
                      uint32_t inode_no, void* io_buf) {
    struct inode* dir_inode = pdir->inode;
    uint32_t block_idx = 0;
    uint32_t* all_blocks = (uint32_t*)sys_malloc(DIR_MAX_BLOCKS * sizeof(uint32_t));
    if (all_blocks == NULL) {
        printk("delete_dir_entry: sys_malloc for all_blocks failed\n");
        return false;
    }

    collect_dir_blocks(part, dir_inode, all_blocks);

    uint32_t dir_entry_size = part->sb->dir_entry_size;
    uint32_t dir_entrys_per_sec = BLOCK_SIZE / dir_entry_size;
    struct dir_entry* dir_e = (struct dir_entry*)io_buf;
    struct dir_entry* dir_entry_found = NULL;
    uint32_t dir_entry_idx;
    uint32_t dir_entry_cnt;
    bool is_dir_first_block = false;

    block_idx = 0;
    while (block_idx < DIR_MAX_BLOCKS) {
        if (all_blocks[block_idx] == 0) {
            block_idx++;
            continue;
        }
        dir_entry_idx = 0;
        memset(io_buf, 0, BLOCK_SIZE);
        ide_read(part->my_disk, blk2lba(part, all_blocks[block_idx]), io_buf, SECTORS_PER_BLOCK);

        /* 遍历该块的目录项 */
        while (dir_entry_idx < dir_entrys_per_sec) {
            if ((dir_e + dir_entry_idx)->f_type != FT_UNKNOWN) {
                if (!strcmp((dir_e + dir_entry_idx)->filename, ".")) {
                    is_dir_first_block = true;
                }
                if ((dir_e + dir_entry_idx)->i_no == inode_no) {
                    dir_entry_found = dir_e + dir_entry_idx;
                    break;
                }
            }
            dir_entry_idx++;
            
        }

        if (dir_entry_found != NULL) {
            /* 找到了，清空该目录项 */
            memset(dir_entry_found, 0, dir_entry_size);
            ide_write(part->my_disk, blk2lba(part, all_blocks[block_idx]), io_buf, SECTORS_PER_BLOCK);

            /* 检查该块是否全空，若是且不是目录的第一个块，则回收 */
            dir_entry_cnt = 0;
            dir_entry_idx = 0;
            while (dir_entry_idx < dir_entrys_per_sec) {
                if ((dir_e + dir_entry_idx)->f_type != FT_UNKNOWN) {
                    dir_entry_cnt++;
                }
                dir_entry_idx++;
            }

            if (dir_entry_cnt == 0 && !is_dir_first_block) {
                /* 回收该数据块 */
                uint32_t block_bitmap_idx = blk2bidx(part, all_blocks[block_idx]);
                bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
                bitmap_sync(part, block_bitmap_idx, BLOCK_BITMAP);

                if (block_idx < INODE_DIRECT_BLOCKS) {
                    dir_inode->i_sectors[block_idx] = 0;
                } else {
                    /* 间接块中的项清零 */
                    uint32_t indirect_blocks = 0;
                    uint32_t indirect_idx = INODE_DIRECT_BLOCKS;
                    while (indirect_idx < DIR_MAX_BLOCKS) {
                        if (all_blocks[indirect_idx] != 0) {
                            indirect_blocks++;
                        }
                        indirect_idx++;
                    }
                    ASSERT(indirect_blocks >= 1);
                    if (indirect_blocks > 1) {
                        all_blocks[block_idx] = 0;
                        ide_write(part->my_disk, blk2lba(part, dir_inode->i_sectors[INODE_SINGLE_INDIRECT]),
                                  all_blocks + INODE_DIRECT_BLOCKS, SECTORS_PER_BLOCK);
                    } else {
                        /* 最后一个间接块也空了，回收间接块表 */
                        block_bitmap_idx = blk2bidx(part, dir_inode->i_sectors[INODE_SINGLE_INDIRECT]);
                        bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
                        bitmap_sync(part, block_bitmap_idx, BLOCK_BITMAP);
                        dir_inode->i_sectors[INODE_SINGLE_INDIRECT] = 0;
                    }
                }
            }

            /* 更新目录 inode 大小并同步 */
            dir_inode->i_size -= dir_entry_size;
            memset(io_buf, 0, BLOCK_SIZE * 2);
            inode_sync(part, dir_inode, io_buf);

            sys_free(all_blocks);
            return true;
        }

        is_dir_first_block = false;
        block_idx++;
    }
    /* 没找到 */
    sys_free(all_blocks);
    return false;
}

/* 读取目录中的下一个目录项 */
struct dir_entry* dir_read(struct dir* dir) {
    struct dir_entry* dir_e = (struct dir_entry*)dir->dir_buf;
    struct inode* dir_inode = dir->inode;
    uint32_t* all_blocks = (uint32_t*)sys_malloc(DIR_MAX_BLOCKS * sizeof(uint32_t));
    if (all_blocks == NULL) {
        return NULL;
    }
    uint32_t block_cnt = DIR_MAX_BLOCKS;
    uint32_t block_idx = 0;
    uint32_t dir_entry_size = cur_part->sb->dir_entry_size;
    uint32_t dir_entrys_per_sec = BLOCK_SIZE / dir_entry_size;
    uint32_t cur_dir_entry_pos = 0;

    /* 收集所有块地址 (直接块 + 一级间接块) */
    collect_dir_blocks(cur_part, dir_inode, all_blocks);

    block_idx = 0;
    while (block_idx < block_cnt) {
        if (all_blocks[block_idx] == 0) {
            block_idx++;
            continue;
        }
        memset(dir_e, 0, BLOCK_SIZE);
        ide_read(cur_part->my_disk, blk2lba(cur_part, all_blocks[block_idx]), dir_e, SECTORS_PER_BLOCK);

        uint32_t dir_entry_idx = 0;
        while (dir_entry_idx < dir_entrys_per_sec) {
            if ((dir_e + dir_entry_idx)->f_type != FT_UNKNOWN) {
                if (cur_dir_entry_pos < dir->dir_pos) {
                    cur_dir_entry_pos += dir_entry_size;
                    dir_entry_idx++;
                    continue;
                }
                ASSERT(cur_dir_entry_pos == dir->dir_pos);
                dir->dir_pos += dir_entry_size;
                sys_free(all_blocks);
                return dir_e + dir_entry_idx;
            }
            dir_entry_idx++;
        }
        block_idx++;
    }
    sys_free(all_blocks);
    return NULL;
}

/* 判断目录是否为空（只有 "." 和 ".."） */
bool dir_is_empty(struct dir* dir) {
    struct inode* dir_inode = dir->inode;
    /* 只有 . 和 .. 时，i_size == 2 * dir_entry_size */
    return (dir_inode->i_size == cur_part->sb->dir_entry_size * 2);
}

/* 在 parent_dir 中删除子目录 child_dir
 * 前提: child_dir 是空目录
 * 返回 0 成功，-1 失败 */
int32_t dir_remove(struct dir* parent_dir, struct dir* child_dir) {
    struct inode* child_dir_inode = child_dir->inode;

    /* 空目录只有第一个块有 "." 和 ".."，其余块应该都是空的 */
    int32_t block_idx = 1;
    while (block_idx < INODE_SECTORS_CNT) {
        ASSERT(child_dir_inode->i_sectors[block_idx] == 0);
        block_idx++;
    }

    void* io_buf = sys_malloc(BLOCK_SIZE * 2);
    if (io_buf == NULL) {
        printk("dir_remove: sys_malloc failed\n");
        return -1;
    }

    /* 从父目录中删除子目录的目录项 */
    delete_dir_entry(cur_part, parent_dir, child_dir_inode->i_no, io_buf);

    /* 回收子目录 inode 和数据块 */
    inode_release(cur_part, child_dir_inode->i_no);

    sys_free(io_buf);
    return 0;
}
