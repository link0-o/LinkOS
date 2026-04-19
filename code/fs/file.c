#include "file.h"
#include "fs.h"
#include "inode.h"
#include "dir.h"
#include "super_block.h"
#include "global.h"
#include "debug.h"
#include "string.h"
#include "interrupt.h"
#include "stdio-kernel.h"
#include "thread.h"
#include "ide.h"

extern void* sys_malloc(uint32_t size);
extern void  sys_free(void* ptr);

/* 全局打开文件表 */
struct file file_table[MAX_FILE_OPEN];

/* 在全局文件表中找到一个空闲位置，返回下标（跳过 0~2 保留给 stdin/out/err） */
int32_t get_free_slot_in_global(void) {
    uint32_t fd_idx = 3;    // 0,1,2 保留
    while (fd_idx < MAX_FILE_OPEN) {
        if (file_table[fd_idx].fd_inode == NULL) {
            return fd_idx;
        }
        fd_idx++;
    }
    printk("exceed max open files\n");
    return -1;
}

/* 将全局文件表下标安装到当前进程的 fd_table 中
 * 返回进程局部 fd 编号 (>= 3)，失败返回 -1 */
int32_t pcb_fd_install(int32_t globa_fd_idx) {
    struct task_struct* cur = running_thread();
    uint8_t local_fd_idx = 3;   // 跳过 stdin/stdout/stderr
    while (local_fd_idx < MAX_FILES_OPEN_PER_PROC) {
        if (cur->fd_table[local_fd_idx] == -1) {
            cur->fd_table[local_fd_idx] = globa_fd_idx;
            return local_fd_idx;
        }
        local_fd_idx++;
    }
    printk("exceed max open files_per_proc\n");
    return -1;
}

/* 从 inode 位图中分配一个空闲 inode，返回 inode 号 */
int32_t inode_bitmap_alloc(struct partition* part) {
    int32_t bit_idx = bitmap_scan(&part->inode_bitmap, 1);
    if (bit_idx == -1) {
        return -1;
    }
    bitmap_set(&part->inode_bitmap, bit_idx, 1);
    return bit_idx;
}

/* 从块位图中分配一个空闲块，返回分区相对块号 (0 = 未分配, 不可能返回 0) */
int32_t block_bitmap_alloc(struct partition* part) {
    int32_t bit_idx = bitmap_scan(&part->block_bitmap, 1);
    if (bit_idx == -1) {
        return -1;
    }
    bitmap_set(&part->block_bitmap, bit_idx, 1);
    /* 转换为分区相对块号: data_start + bit_idx */
    return bidx2blk(part, bit_idx);
}

/* 将位图的变化同步回磁盘
 * bit_idx: 位图中的位下标
 * btmp_type: BLOCK_BITMAP 或 INODE_BITMAP */
void bitmap_sync(struct partition* part, uint32_t bit_idx, uint8_t btmp_type) {
    uint32_t off_blk  = bit_idx / BITS_PER_BLOCK;      // 位所在的块偏移
    uint32_t off_size = off_blk * BLOCK_SIZE;           // 字节偏移

    uint32_t sec_lba;
    uint8_t* bitmap_off;

    switch (btmp_type) {
        case BLOCK_BITMAP:
            sec_lba    = blk2lba(part, part->sb->block_bitmap_start + off_blk);
            bitmap_off = part->block_bitmap.bits + off_size;
            break;
        case INODE_BITMAP:
            sec_lba    = blk2lba(part, part->sb->inode_bitmap_start + off_blk);
            bitmap_off = part->inode_bitmap.bits + off_size;
            break;
    }
    ide_write(part->my_disk, sec_lba, bitmap_off, SECTORS_PER_BLOCK);
}

/* 创建文件, 成功返回文件描述符, 失败返回 -1 */
int32_t file_create(struct dir* parent_dir, char* filename, uint8_t flag) {
    /* 用于后续操作的缓冲区 */
    void* io_buf = sys_malloc(BLOCK_SIZE * 2);    // 2 个块
    if (io_buf == NULL) {
        printk("in file_create: sys_malloc for io_buf failed\n");
        return -1;
    }

    uint8_t rollback_step = 0;     // 回滚标记

    /* 1. 分配 inode */
    int32_t inode_no = inode_bitmap_alloc(cur_part);
    if (inode_no == -1) {
        printk("in file_create: allocate inode failed\n");
        return -1;
    }

    /* 确保在内核空间分配 inode（所有任务共享） */
    struct task_struct* cur = running_thread();
    uint32_t* cur_pagedir_bak = cur->pgdir;
    cur->pgdir = NULL;
    struct inode* new_file_inode = (struct inode*)sys_malloc(sizeof(struct inode));
    cur->pgdir = cur_pagedir_bak;

    if (new_file_inode == NULL) {
        printk("in file_create: sys_malloc for inode failed\n");
        rollback_step = 1;
        goto rollback;
    }
    inode_init(inode_no, new_file_inode);

    /* 2. 在全局文件表中分配一个位置 */
    int fd_idx = get_free_slot_in_global();
    if (fd_idx == -1) {
        printk("exceed max open files\n");
        rollback_step = 2;
        goto rollback;
    }

    file_table[fd_idx].fd_inode = new_file_inode;
    file_table[fd_idx].fd_pos   = 0;
    file_table[fd_idx].fd_flag  = flag;

    /* 3. 创建目录项 */
    struct dir_entry new_dir_entry;
    memset(&new_dir_entry, 0, sizeof(struct dir_entry));
    create_dir_entry(filename, inode_no, FT_REGULAR, &new_dir_entry);

    /* 4. 将目录项写入父目录 */
    if (!sync_dir_entry(parent_dir, &new_dir_entry, io_buf)) {
        printk("sync dir_entry to disk failed\n");
        rollback_step = 3;
        goto rollback;
    }

    /* 5. 同步父目录 inode 到磁盘 */
    memset(io_buf, 0, BLOCK_SIZE * 2);
    inode_sync(cur_part, parent_dir->inode, io_buf);

    /* 6. 同步新文件 inode 到磁盘 */
    memset(io_buf, 0, BLOCK_SIZE * 2);
    inode_sync(cur_part, new_file_inode, io_buf);

    /* 7. 同步 inode 位图到磁盘 */
    bitmap_sync(cur_part, inode_no, INODE_BITMAP);

    /* 8. 将新 inode 加入 open_inodes 链表 */
    list_push(&cur_part->open_inodes, &new_file_inode->inode_tag);
    new_file_inode->i_open_cnts = 1;

    sys_free(io_buf);

    /* 9. 安装到当前进程的 fd_table 中 */
    return pcb_fd_install(fd_idx);

rollback:
    switch (rollback_step) {
        case 3:
            memset(&file_table[fd_idx], 0, sizeof(struct file));
            /* fall through */
        case 2: {
            struct task_struct* cur2 = running_thread();
            uint32_t* pagedir_bak = cur2->pgdir;
            cur2->pgdir = NULL;
            sys_free(new_file_inode);
            cur2->pgdir = pagedir_bak;
            /* fall through */
        }
        case 1:
            bitmap_set(&cur_part->inode_bitmap, inode_no, 0);
            break;
    }
    sys_free(io_buf);
    return -1;
}

/* 打开 inode_no 对应的文件, 返回文件描述符 */
int32_t file_open(uint32_t inode_no, uint8_t flag) {
    int fd_idx = get_free_slot_in_global();
    if (fd_idx == -1) {
        printk("exceed max open files\n");
        return -1;
    }
    file_table[fd_idx].fd_inode = inode_open(cur_part, inode_no);
    file_table[fd_idx].fd_pos   = 0;       // 打开文件时，读写位置重置为 0
    file_table[fd_idx].fd_flag  = flag;

    bool* write_deny = &file_table[fd_idx].fd_inode->write_deny;

    /* 如果要写文件，检查是否有其他进程正在写 */
    if (flag & O_WRONLY || flag & O_RDWR) {
        enum intr_status old_status = intr_disable();
        if (!(*write_deny)) {
            *write_deny = true;
            intr_set_status(old_status);
        } else {
            intr_set_status(old_status);
            printk("file can't be write now, try again later\n");
            return -1;
        }
    }
    return pcb_fd_install(fd_idx);
}

/* 关闭文件 */
int32_t file_close(struct file* file) {
    if (file == NULL) {
        return -1;
    }
    if (file->fd_flag & O_WRONLY || file->fd_flag & O_RDWR) {
        file->fd_inode->write_deny = false;
    }
    inode_close(file->fd_inode);
    file->fd_inode = NULL;      // 标志文件表项空闲
    return 0;
}

/* 写文件，将 buf 中 count 字节写入 file (追加写)
 * 使用多级块索引辅助函数, 支持直接块 + 一/二/三级间接块
 * 返回写入的字节数，失败返回 -1 */
int32_t file_write(struct file* file, const void* buf, uint32_t count) {
    if (count > FILE_MAX_SIZE - file->fd_inode->i_size) {
        printk("exceed max file_size %u bytes, write file failed\n", FILE_MAX_SIZE);
        return -1;
    }

    uint8_t* io_buf = sys_malloc(BLOCK_SIZE);
    if (io_buf == NULL) {
        printk("file_write: sys_malloc for io_buf failed\n");
        return -1;
    }

    /* 追加写: 从文件末尾开始 */
    file->fd_pos = file->fd_inode->i_size;
    uint32_t bytes_written = 0;
    uint32_t size_left = count;

    while (bytes_written < count) {
        uint32_t sec_idx   = file->fd_pos / BLOCK_SIZE;
        uint32_t sec_off   = file->fd_pos % BLOCK_SIZE;
        uint32_t sec_left  = BLOCK_SIZE - sec_off;
        uint32_t chunk_size = (size_left < sec_left) ? size_left : sec_left;

        /* 获取分区相对块号, 如果未分配则分配新块 */
        uint32_t blk_no = get_data_block(cur_part, file->fd_inode, sec_idx);
        if (blk_no == 0) {
            blk_no = alloc_data_block(cur_part, file->fd_inode, sec_idx);
            if (blk_no == 0) {
                printk("file_write: alloc_data_block failed at block %d\n", sec_idx);
                break;
            }
        }

        memset(io_buf, 0, BLOCK_SIZE);
        if (sec_off != 0) {
            ide_read(cur_part->my_disk, blk2lba(cur_part, blk_no), io_buf, SECTORS_PER_BLOCK);
        }
        memcpy(io_buf + sec_off, (uint8_t*)buf + bytes_written, chunk_size);
        ide_write(cur_part->my_disk, blk2lba(cur_part, blk_no), io_buf, SECTORS_PER_BLOCK);

        bytes_written += chunk_size;
        size_left -= chunk_size;
        file->fd_pos += chunk_size;
    }

    file->fd_inode->i_size = file->fd_pos;

    /* 同步 inode 到磁盘 (需要 2 个扇区的缓冲区以防 inode 跨扇区) */
    void* sync_buf = sys_malloc(BLOCK_SIZE * 2);
    if (sync_buf != NULL) {
        inode_sync(cur_part, file->fd_inode, sync_buf);
        sys_free(sync_buf);
    }

    sys_free(io_buf);
    return bytes_written;
}

/* 读文件，从 file 中读取 count 字节到 buf
 * 使用多级块索引辅助函数, 按需读取每个逻辑块
 * 返回实际读到的字节数, 0 表示到达文件末尾, -1 表示失败 */
int32_t file_read(struct file* file, void* buf, uint32_t count) {
    uint32_t size = count;
    /* 如果剩余字节不足 count, 只读到文件末尾 */
    if ((file->fd_pos + count) > file->fd_inode->i_size) {
        size = file->fd_inode->i_size - file->fd_pos;
        if (size == 0) {
            return 0;   // 到达文件末尾
        }
    }

    uint8_t* io_buf = sys_malloc(BLOCK_SIZE);
    if (io_buf == NULL) {
        return -1;
    }

    uint32_t bytes_read = 0;
    uint32_t size_left = size;

    while (bytes_read < size) {
        uint32_t sec_idx   = file->fd_pos / BLOCK_SIZE;
        uint32_t sec_off   = file->fd_pos % BLOCK_SIZE;
        uint32_t sec_left  = BLOCK_SIZE - sec_off;
        uint32_t chunk_size = (size_left < sec_left) ? size_left : sec_left;

        /* 通过多级索引辅助函数获取分区相对块号 */
        uint32_t blk_no = get_data_block(cur_part, file->fd_inode, sec_idx);
        if (blk_no == 0) {
            break;  // 该块未分配 (理论上不应该发生)
        }

        ide_read(cur_part->my_disk, blk2lba(cur_part, blk_no), io_buf, SECTORS_PER_BLOCK);
        memcpy((uint8_t*)buf + bytes_read, io_buf + sec_off, chunk_size);

        bytes_read += chunk_size;
        size_left  -= chunk_size;
        file->fd_pos += chunk_size;
    }

    sys_free(io_buf);
    return bytes_read;
}
