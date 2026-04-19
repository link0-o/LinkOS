#include "fs.h"
#include "super_block.h"
#include "inode.h"
#include "dir.h"
#include "file.h"
#include "ide.h"
#include "global.h"
#include "debug.h"
#include "string.h"
#include "list.h"
#include "stdio-kernel.h"
#include "thread.h"
#include "console.h"
#include "print.h"
#include "ioqueue.h"

extern struct ioqueue kbd_buf;   /* 键盘缓冲区, 定义在 keyboard.c */

extern void* sys_malloc(uint32_t size);
extern void  sys_free(void* ptr);

struct partition* cur_part;     // 当前挂载的分区

/* =============== 格式化相关 =============== */

/* 格式化分区: 在 part 上创建文件系统
 *
 * 磁盘布局 (以 4KB 块为单位, 块号相对于分区起始):
 * ┌───────────┬────────────┬─────────────┬────────────┬───────────┐
 * │Block0     │BlockBMP(n) │InodeBMP(n)  │InodeTbl(n) │ 数据区    │
 * │(boot+SB)  │            │             │            │           │
 * └───────────┴────────────┴─────────────┴────────────┴───────────┘
 *  Block 0: sector 0=引导扇区, sector 1=超级块, sectors 2-7=保留
 * BMP -> bitmap    tbl -> table
 */
static void partition_format(struct partition* part) {
    /* === 以块(4KB)为单位计算布局 === */
    uint32_t total_blks = part->sec_cnt / SECTORS_PER_BLOCK;

    /* inode 位图占的块数 */
    uint32_t inode_bitmap_blks = DIV_ROUND_UP(MAX_FILES_PER_PART, BITS_PER_BLOCK);
    /* inode 表占的块数 */
    uint32_t inode_table_blks = DIV_ROUND_UP(
        MAX_FILES_PER_PART * sizeof(struct inode), BLOCK_SIZE);
    /* 已使用的块数 (不含块位图, 因为块位图本身也占块) */
    uint32_t used_blks = 1 + inode_bitmap_blks + inode_table_blks;  /* 1 = boot block */
    uint32_t free_blks = total_blks - used_blks;

    /* 块位图占的块数 (迭代求解) */
    uint32_t block_bitmap_blks = DIV_ROUND_UP(free_blks, BITS_PER_BLOCK);
    uint32_t block_bitmap_bit_len = free_blks - block_bitmap_blks;
    block_bitmap_blks = DIV_ROUND_UP(block_bitmap_bit_len, BITS_PER_BLOCK);

    /* ===== 1. 初始化超级块 (所有地址均为分区相对块号) ===== */
    struct super_block sb;
    sb.magic           = FS_MAGIC;
    sb.sec_cnt         = part->sec_cnt;
    sb.inode_cnt       = MAX_FILES_PER_PART;
    sb.block_size      = BLOCK_SIZE;

    sb.block_bitmap_start = 1;  /* block 0 = boot + super block */
    sb.block_bitmap_blks  = block_bitmap_blks;

    sb.inode_bitmap_start = sb.block_bitmap_start + sb.block_bitmap_blks;
    sb.inode_bitmap_blks  = inode_bitmap_blks;

    sb.inode_table_start  = sb.inode_bitmap_start + sb.inode_bitmap_blks;
    sb.inode_table_blks   = inode_table_blks;

    sb.data_start         = sb.inode_table_start + sb.inode_table_blks;
    sb.root_inode_no      = 0;
    sb.dir_entry_size     = sizeof(struct dir_entry);

    printk("   %s info:\n", part->name);
    printk("      magic:0x%x\n      block_size:%d\n", sb.magic, sb.block_size);
    printk("      total_blocks:0x%x\n      inode_cnt:0x%x\n", total_blks, sb.inode_cnt);
    printk("      block_bitmap_start:%d\n      block_bitmap_blks:%d\n",
           sb.block_bitmap_start, sb.block_bitmap_blks);
    printk("      inode_bitmap_start:%d\n      inode_table_start:%d\n",
           sb.inode_bitmap_start, sb.inode_table_start);
    printk("      data_start:%d\n", sb.data_start);

    struct disk* hd = part->my_disk;

    /* ===== 2. 将超级块写入 block 0 的第 1 个扇区 ===== */
    ide_write(hd, part->start_lba + 1, &sb, 1);
    printk("      super_block_lba:0x%x\n", part->start_lba + 1);

    /* 分配缓冲区: 取位图中较大者 × BLOCK_SIZE */
    uint32_t buf_size = (sb.block_bitmap_blks >= sb.inode_bitmap_blks ?
                         sb.block_bitmap_blks : sb.inode_bitmap_blks);
    buf_size *= BLOCK_SIZE;
    uint8_t* buf = (uint8_t*)sys_malloc(buf_size);
    if (buf == NULL) {
        printk("partition_format: sys_malloc for buf failed!\n");
        return;
    }

    /* ===== 3. 将块位图初始化并写入磁盘 ===== */
    memset(buf, 0, buf_size);
    buf[0] |= 0x01;    // bit 0 = 根目录的第一个数据块

    /* 位图末尾无效位标记为已使用 */
    uint32_t block_bitmap_last_byte = block_bitmap_bit_len / 8;
    uint8_t  block_bitmap_last_bit  = block_bitmap_bit_len % 8;
    uint32_t last_size = BLOCK_SIZE - (block_bitmap_last_byte % BLOCK_SIZE) - 1;

    memset(&buf[block_bitmap_last_byte + 1], 0xff, last_size);
    /* 从第一个无效位开始标记为已使用 */
    uint8_t bit_idx = block_bitmap_last_bit;
    while (bit_idx < 8) {
        buf[block_bitmap_last_byte] |= (1 << bit_idx);
        bit_idx++;
    }

    ide_write(hd, blk2lba(part, sb.block_bitmap_start),
             buf, sb.block_bitmap_blks * SECTORS_PER_BLOCK);

    /* ===== 4. 将 inode 位图初始化并写入磁盘 ===== */
    memset(buf, 0, buf_size);
    buf[0] |= 0x01;    // inode 0 分配给根目录
    ide_write(hd, blk2lba(part, sb.inode_bitmap_start),
             buf, sb.inode_bitmap_blks * SECTORS_PER_BLOCK);

    /* ===== 5. 将 inode 表初始化并写入磁盘 (分块写入) ===== */
    /* 第一个块包含根目录的 inode (inode 0) */
    memset(buf, 0, BLOCK_SIZE);
    struct inode* i = (struct inode*)buf;
    i->i_size = sb.dir_entry_size * 2;  // . 和 .. 两个目录项
    i->i_no = 0;
    i->i_sectors[0] = sb.data_start;
    ide_write(hd, blk2lba(part, sb.inode_table_start), buf, SECTORS_PER_BLOCK);

    /* 其余块全部清零 */
    uint32_t chunk_blks = buf_size / BLOCK_SIZE;
    memset(buf, 0, buf_size);
    uint32_t remaining = sb.inode_table_blks - 1;
    uint32_t cur_blk = sb.inode_table_start + 1;
    while (remaining > 0) {
        uint32_t blks_to_write = (remaining > chunk_blks) ? chunk_blks : remaining;
        ide_write(hd, blk2lba(part, cur_blk), buf, blks_to_write * SECTORS_PER_BLOCK);
        cur_blk += blks_to_write;
        remaining -= blks_to_write;
    }

    /* ===== 6. 将根目录的数据块写入磁盘 ===== */
    memset(buf, 0, BLOCK_SIZE);
    struct dir_entry* p_de = (struct dir_entry*)buf;
    /* "." 目录项 */
    memcpy(p_de->filename, ".", 2);
    p_de->i_no = 0;
    p_de->f_type = FT_DIRECTORY;
    p_de++;

    /* ".." 目录项 (根目录的父目录指向自己) */
    memcpy(p_de->filename, "..", 3);
    p_de->i_no = 0;
    p_de->f_type = FT_DIRECTORY;

    ide_write(hd, blk2lba(part, sb.data_start), buf, SECTORS_PER_BLOCK);

    printk("      root_dir_blk:%d\n", sb.data_start);
    printk("   %s format done\n", part->name);
    sys_free(buf);
}

/* =============== 挂载 / 初始化 =============== */

/* list_traversal 回调: 找到名为 "sdb1" 的分区用于默认挂载 */
static bool mount_partition(struct list_elem* pelem, int arg) {
    char* part_name = (char*)arg;
    struct partition* part = elem2entry(struct partition, part_tag, pelem);
    if (!strcmp(part->name, part_name)) {
        cur_part = part;
        struct disk* hd = cur_part->my_disk;

        /* 读取该分区的超级块 */
        struct super_block* sb_buf = (struct super_block*)sys_malloc(SECTOR_SIZE);
        if (sb_buf == NULL) {
            PANIC("alloc memory for sb_buf failed!");
        }
        cur_part->sb = (struct super_block*)sys_malloc(sizeof(struct super_block));
        if (cur_part->sb == NULL) {
            PANIC("alloc memory failed!");
        }
        memset(sb_buf, 0, SECTOR_SIZE);
        ide_read(hd, cur_part->start_lba + 1, sb_buf, 1);
        memcpy(cur_part->sb, sb_buf, sizeof(struct super_block));

        /* 读取块位图到内存 */
        cur_part->block_bitmap.bits =
            (uint8_t*)sys_malloc(sb_buf->block_bitmap_blks * BLOCK_SIZE);
        if (cur_part->block_bitmap.bits == NULL) {
            PANIC("alloc memory failed!");
        }
        cur_part->block_bitmap.btmp_bytes_len =
            sb_buf->block_bitmap_blks * BLOCK_SIZE;
        ide_read(hd, blk2lba(cur_part, sb_buf->block_bitmap_start),
                 cur_part->block_bitmap.bits, sb_buf->block_bitmap_blks * SECTORS_PER_BLOCK);

        /* 读取 inode 位图到内存 */
        cur_part->inode_bitmap.bits =
            (uint8_t*)sys_malloc(sb_buf->inode_bitmap_blks * BLOCK_SIZE);
        if (cur_part->inode_bitmap.bits == NULL) {
            PANIC("alloc memory failed!");
        }
        cur_part->inode_bitmap.btmp_bytes_len =
            sb_buf->inode_bitmap_blks * BLOCK_SIZE;
        ide_read(hd, blk2lba(cur_part, sb_buf->inode_bitmap_start),
                 cur_part->inode_bitmap.bits, sb_buf->inode_bitmap_blks * SECTORS_PER_BLOCK);

        /* 初始化该分区的已打开 inode 链表 */
        list_init(&cur_part->open_inodes);

        printk("mount %s done!\n", part->name);

        sys_free(sb_buf);
        return true;    // 返回 true 让 list_traversal 停止遍历
    }
    return false;
}

/* 文件系统初始化:
 * 1. 遍历所有分区, 检查是否已格式化 (通过超级块的 magic 判断)
 * 2. 未格式化的分区执行 partition_format
 * 3. 挂载默认分区 (sdb1)
 * 4. 打开根目录
 * 5. 初始化全局文件表 */
void filesys_init(void) {
    printk("filesys_init start\n");

    uint8_t channel_no = 0;
    uint8_t dev_no     = 0;
    uint8_t part_idx   = 0;

    /* 用来读超级块，判断是否已格式化 */
    struct super_block* sb_buf = (struct super_block*)sys_malloc(SECTOR_SIZE);
    if (sb_buf == NULL) {
        PANIC("alloc memory failed!");
    }

    printk("   searching filesystem...\n");
    while (channel_no < channel_cnt) {
        dev_no = 0;
        while (dev_no < 2) {
            if (dev_no == 0) {
                /* 跳过裸盘（系统盘 hd60M.img 无分区表） */
                dev_no++;
                continue;
            }
            struct disk* hd = &channels[channel_no].devices[dev_no];

            /* 扫描主分区 */
            struct partition* part = hd->prim_parts;
            part_idx = 0;
            while (part_idx < 4) {
                if (part->sec_cnt != 0) {
                    memset(sb_buf, 0, SECTOR_SIZE);
                    ide_read(hd, part->start_lba + 1, sb_buf, 1);
                    if (sb_buf->magic == FS_MAGIC) {
                        printk("   %s has filesystem\n", part->name);
                    } else {
                        printk("   formatting %s's partition %s...\n",
                               hd->name, part->name);
                        partition_format(part);
                    }
                }
                part_idx++;
                part++;
            }

            /* 扫描逻辑分区 */
            part = hd->logic_parts;
            part_idx = 0;
            while (part_idx < 8) {
                if (part->sec_cnt != 0) {
                    memset(sb_buf, 0, SECTOR_SIZE);
                    ide_read(hd, part->start_lba + 1, sb_buf, 1);
                    if (sb_buf->magic == FS_MAGIC) {
                        printk("   %s has filesystem\n", part->name);
                    } else {
                        printk("   formatting %s's partition %s...\n",
                               hd->name, part->name);
                        partition_format(part);
                    }
                }
                part_idx++;
                part++;
            }
            dev_no++;
        }
        channel_no++;
    }
    sys_free(sb_buf);

    /* 挂载 sdb1 分区 (默认分区) */
    char default_part[8] = "sdb1";
    list_traversal(&partition_list, mount_partition, (int)default_part);

    /* 打开根目录 */
    open_root_dir(cur_part);

    /* 初始化全局文件表 */
    uint32_t fd_idx = 0;
    while (fd_idx < MAX_FILE_OPEN) {
        file_table[fd_idx].fd_inode = NULL;
        fd_idx++;
    }

    printk("filesys_init done\n");
}

/* =============== 路径解析 =============== */

/* 将进程局部 fd 转换为全局文件表下标 */
uint32_t fd_local2global(uint32_t local_fd) {
    struct task_struct* cur = running_thread();
    int32_t global_fd = cur->fd_table[local_fd];
    ASSERT(global_fd >= 0 && global_fd < MAX_FILE_OPEN);
    return (uint32_t)global_fd;
}

/* 从路径中解析出最顶层的目录/文件名，写入 name_store
 * 返回剩余子路径指针
 * 例: "/a/b/c" → name_store="a", 返回 "/b/c" */
char* path_parse(char* pathname, char* name_store) {
    if (pathname[0] == '/') {
        /* 跳过路径开头的 '/' （可能有多个） */
        while (*(++pathname) == '/');
    }

    /* 复制直到下一个 '/' 或字符串结尾 */
    while (*pathname != '/' && *pathname != '\0') {
        *name_store++ = *pathname++;
    }
    *name_store = '\0';

    if (pathname[0] == '\0') {
        return NULL;    // 路径解析完毕
    }
    return pathname;
}

/* 计算路径深度, 如 "/a/b/c" 深度为 3 */
int32_t path_depth_cnt(char* pathname) {
    ASSERT(pathname != NULL);
    char* p = pathname;
    char name[MAX_FILE_NAME_LEN];
    uint32_t depth = 0;

    p = path_parse(p, name);
    while (name[0]) {
        depth++;
        memset(name, 0, MAX_FILE_NAME_LEN);
        if (p) {
            p = path_parse(p, name);
        }
    }
    return depth;
}

/* 搜索文件路径 pathname, 找到返回 inode 号, 否则返回 -1
 * searched_record 记录搜索过程信息 (父目录, 类型等) */
int32_t search_file(const char* pathname,
                    struct path_search_record* searched_record) {
    /* 特殊处理: 如果是根目录 "/", "/.", "/.." */
    if (!strcmp(pathname, "/") || !strcmp(pathname, "/.") ||
        !strcmp(pathname, "/..")) {
        searched_record->parent_dir = &root_dir;
        searched_record->file_type  = FT_DIRECTORY;
        searched_record->searched_path[0] = '\0';
        return 0;
    }

    uint32_t path_len = strlen(pathname);
    ASSERT(pathname[0] == '/' && path_len > 1 && path_len < MAX_PATH_LEN);

    char* sub_path = (char*)pathname;
    struct dir* parent_dir = &root_dir;
    struct dir_entry dir_e;

    char name[MAX_FILE_NAME_LEN] = {0};

    searched_record->parent_dir = parent_dir;
    searched_record->file_type  = FT_UNKNOWN;
    uint32_t parent_inode_no = 0;       // 父目录 inode (根目录为 0)

    sub_path = path_parse(sub_path, name);
    while (name[0]) {
        ASSERT(strlen(searched_record->searched_path) < MAX_PATH_LEN);
        strcat(searched_record->searched_path, "/");
        strcat(searched_record->searched_path, name);

        /* 在当前目录中搜索 */
        if (search_dir_entry(cur_part, parent_dir, name, &dir_e)) {
            memset(name, 0, MAX_FILE_NAME_LEN);
            if (sub_path) {
                sub_path = path_parse(sub_path, name);
            }

            if (dir_e.f_type == FT_DIRECTORY) {
                /* 是目录, 继续深入 */
                parent_inode_no = parent_dir->inode->i_no;
                dir_close(parent_dir);
                parent_dir = dir_open(cur_part, dir_e.i_no);        // 此时 parent_dir 是当前目录
                searched_record->parent_dir = parent_dir;
                continue;
            } else if (dir_e.f_type == FT_REGULAR) {
                /* 是普通文件 */
                searched_record->file_type = FT_REGULAR;
                return dir_e.i_no;
            }
        } else {
            /* 找不到, 直接返回 -1
             * 此时 searched_record->parent_dir 保存的是最深层可达的父目录 */
            return -1;
        }
    }

    /* 到这里说明 pathname 的最后一个分量是目录 */
    dir_close(searched_record->parent_dir);
    searched_record->parent_dir = dir_open(cur_part, parent_inode_no);
    searched_record->file_type  = FT_DIRECTORY;
    return dir_e.i_no;
}

/* =============== 系统调用实现 =============== */

/* sys_open: 打开或创建文件 */
int32_t sys_open(const char* pathname, uint8_t flags) {
    /* 如果最后一个字符是 '/', 说明打开的是目录, 应用 opendir */
    if (pathname[strlen(pathname) - 1] == '/') {
        printk("can't open a directory with open(), use opendir()\n");
        return -1;
    }

    ASSERT(flags <= 7);     // O_RDONLY|O_WRONLY|O_RDWR|O_CREAT 范围检查
    int32_t fd = -1;

    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));

    /* 计算路径深度 */
    uint32_t pathname_depth = path_depth_cnt((char*)pathname);

    /* 搜索文件 */
    int32_t inode_no = search_file(pathname, &searched_record);
    bool found = (inode_no != -1);

    if (searched_record.file_type == FT_DIRECTORY) {
        printk("can't open a directory with open(), use opendir() to instead\n");
        dir_close(searched_record.parent_dir);
        return -1;
    }

    uint32_t path_searched_depth = path_depth_cnt(searched_record.searched_path);
    if (pathname_depth != path_searched_depth) {
        /* 中间某层目录不存在 */
        printk("cannot access %s: Not a directory, subpath %s is't exist\n",
               pathname, searched_record.searched_path);
        dir_close(searched_record.parent_dir);
        return -1;
    }

    if (!found && !(flags & O_CREAT)) {
        /* 文件不存在且不创建 */
        printk("in path %s, file %s is't exist\n",
               searched_record.searched_path, strrchr(searched_record.searched_path, '/') + 1);
        dir_close(searched_record.parent_dir);
        return -1;
    } else if (found && (flags & O_CREAT)) {
        /* 文件已存在但要求创建 */
        printk("%s has already exist!\n", pathname);
        dir_close(searched_record.parent_dir);
        return -1;
    }

    switch (flags & O_CREAT) {
        case O_CREAT:
            printk("creating file\n");
            fd = file_create(searched_record.parent_dir,
                            strrchr(pathname, '/') + 1, flags);
            dir_close(searched_record.parent_dir);
            break;
        default:
            /* 其他情况: 打开已存在的文件 */
            fd = file_open(inode_no, flags);
    }
    return fd;
}

/* sys_close: 关闭文件描述符 */
int32_t sys_close(int32_t fd) {
    int32_t ret = -1;
    if (fd > 2) {   // 不能关闭 stdin/stdout/stderr
        uint32_t global_fd = fd_local2global(fd);
        ret = file_close(&file_table[global_fd]);
        running_thread()->fd_table[fd] = -1;    // 释放进程 fd_table 槽位
    }
    return ret;
}

/* sys_write: 写文件 (增强版, 支持 stdout/stderr 和普通文件) */
int32_t sys_write(int32_t fd, const void* buf, uint32_t count) {
    if (fd < 0) {
        printk("sys_write: fd error\n");
        return -1;
    }
    if (fd == stdout_no || fd == stderr_no) {
        /* 标准输出/错误: 直接写控制台 */
        char tmp_buf[1024] = {0};
        memcpy(tmp_buf, buf, count);
        console_put_str(tmp_buf);
        return count;
    }

    uint32_t _fd = fd_local2global(fd);
    struct file* wr_file = &file_table[_fd];
    if (wr_file->fd_flag & O_WRONLY || wr_file->fd_flag & O_RDWR) {
        uint32_t bytes_written = file_write(wr_file, buf, count);
        return bytes_written;
    } else {
        console_put_str("sys_write: not allowed to write file without flag O_WRONLY or O_RDWR\n");
        return -1;
    }
}

/* sys_read: 读文件 */
int32_t sys_read(int32_t fd, void* buf, uint32_t count) {
    ASSERT(buf != NULL);
    int32_t ret = -1;
    if (fd < 0 || fd == stdout_no || fd == stderr_no) {
        printk("sys_read: fd error\n");
    } else if (fd == stdin_no) {
        /* 从键盘缓冲区读取 */
        char* dst = (char*)buf;
        uint32_t bytes_read = 0;
        while (bytes_read < count) {
            dst[bytes_read] = ioq_getchar(&kbd_buf);
            bytes_read++;
        }
        ret = (int32_t)bytes_read;
    } else {
        uint32_t _fd = fd_local2global(fd);
        ret = file_read(&file_table[_fd], buf, count);
    }
    return ret;
}

/* sys_lseek: 改变文件读写位置 */
int32_t sys_lseek(int32_t fd, int32_t offset, uint8_t whence) {
    if (fd < 0) {
        printk("sys_lseek: fd error\n");
        return -1;
    }
    ASSERT(whence > 0 && whence < 4);
    uint32_t _fd = fd_local2global(fd);
    struct file* pf = &file_table[_fd];
    int32_t new_pos = 0;
    int32_t file_size = (int32_t)pf->fd_inode->i_size;

    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = (int32_t)pf->fd_pos + offset;
            break;
        case SEEK_END:
            new_pos = file_size + offset;
            break;
    }
    if (new_pos < 0 || new_pos > file_size) {
        return -1;
    }
    pf->fd_pos = new_pos;
    return pf->fd_pos;
}

/* sys_unlink: 删除文件 (非目录) */
int32_t sys_unlink(const char* pathname) {
    ASSERT(strlen(pathname) < MAX_PATH_LEN);

    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));
    int32_t inode_no = search_file(pathname, &searched_record);
    ASSERT(inode_no != 0);      // 不能删根目录

    if (inode_no == -1) {
        printk("file %s not found!\n", pathname);
        dir_close(searched_record.parent_dir);
        return -1;
    }

    if (searched_record.file_type == FT_DIRECTORY) {
        printk("can't delete a directory with unlink(), use rmdir()\n");
        dir_close(searched_record.parent_dir);
        return -1;
    }

    /* 检查是否有进程正在使用该文件 */
    uint32_t file_idx = 0;
    while (file_idx < MAX_FILE_OPEN) {
        if (file_table[file_idx].fd_inode != NULL &&
            (uint32_t)file_table[file_idx].fd_inode->i_no == (uint32_t)inode_no) {
            printk("file %s is in use, not allow to delete!\n", pathname);
            dir_close(searched_record.parent_dir);
            return -1;
        }
        file_idx++;
    }

    void* io_buf = sys_malloc(BLOCK_SIZE * 2);
    if (io_buf == NULL) {
        dir_close(searched_record.parent_dir);
        return -1;
    }

    /* 从父目录中删除目录项 */
    delete_dir_entry(cur_part, searched_record.parent_dir, inode_no, io_buf);

    /* 回收该文件的所有资源 (块 + inode) */
    inode_release(cur_part, inode_no);

    sys_free(io_buf);
    dir_close(searched_record.parent_dir);
    return 0;
}

/* sys_mkdir: 创建目录 */
int32_t sys_mkdir(const char* pathname) {
    uint8_t rollback_step = 0;
    void* io_buf = sys_malloc(BLOCK_SIZE * 2);              // 方便处理跨页情况, 目录项和 inode 同步
    if (io_buf == NULL) {
        printk("sys_mkdir: sys_malloc for io_buf failed\n");
        return -1;
    }

    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));
    int32_t inode_no = search_file(pathname, &searched_record);

    if (inode_no != -1) {
        /* 已存在同名文件/目录 */
        printk("sys_mkdir: file or directory %s exist!\n", pathname);
        dir_close(searched_record.parent_dir);
        sys_free(io_buf);
        return -1;
    }

    /* 检查搜索到的深度是否正确（中间路径必须全部存在） */
    uint32_t pathname_depth = path_depth_cnt((char*)pathname);
    uint32_t path_searched_depth = path_depth_cnt(searched_record.searched_path);
    if (pathname_depth != path_searched_depth) {
        printk("sys_mkdir: can't access %s: subpath %s isn't exist\n",
               pathname, searched_record.searched_path);
        dir_close(searched_record.parent_dir);
        sys_free(io_buf);
        return -1;
    }

    char* dirname = strrchr(searched_record.searched_path, '/') + 1;

    /* 1. 分配 inode */
    inode_no = inode_bitmap_alloc(cur_part);
    if (inode_no == -1) {
        printk("sys_mkdir: allocate inode failed\n");
        dir_close(searched_record.parent_dir);
        sys_free(io_buf);
        return -1;
    }

    /* 2. 分配数据块(用于存储 . 和 ..) */
    struct inode new_dir_inode;
    inode_init(inode_no, &new_dir_inode);

    uint32_t block_no = block_bitmap_alloc(cur_part);
    if (block_no == -1) {
        printk("sys_mkdir: block_bitmap_alloc for create directory failed\n");
        rollback_step = 1;
        goto rollback;
    }
    new_dir_inode.i_sectors[0] = block_no;
    bitmap_sync(cur_part, blk2bidx(cur_part, block_no), BLOCK_BITMAP);

    /* 3. 写入 . 和 .. 目录项 */
    memset(io_buf, 0, BLOCK_SIZE * 2);
    struct dir_entry* p_de = (struct dir_entry*)io_buf;
    /* "." 指向自己 */
    memcpy(p_de->filename, ".", 2);
    p_de->i_no = inode_no;
    p_de->f_type = FT_DIRECTORY;
    p_de++;
    /* ".." 指向父目录 */
    memcpy(p_de->filename, "..", 3);
    p_de->i_no = searched_record.parent_dir->inode->i_no;
    p_de->f_type = FT_DIRECTORY;

    ide_write(cur_part->my_disk, blk2lba(cur_part, new_dir_inode.i_sectors[0]), io_buf, SECTORS_PER_BLOCK);

    new_dir_inode.i_size = 2 * cur_part->sb->dir_entry_size;

    /* 4. 在父目录中添加新目录的目录项 */
    struct dir_entry new_dir_entry;
    memset(&new_dir_entry, 0, sizeof(struct dir_entry));
    create_dir_entry(dirname, inode_no, FT_DIRECTORY, &new_dir_entry);
    memset(io_buf, 0, BLOCK_SIZE * 2);
    if (!sync_dir_entry(searched_record.parent_dir, &new_dir_entry, io_buf)) {
        printk("sys_mkdir: sync_dir_entry to disk failed!\n");
        rollback_step = 2;
        goto rollback;
    }

    /* 5. 同步父目录 inode */
    memset(io_buf, 0, BLOCK_SIZE * 2);
    inode_sync(cur_part, searched_record.parent_dir->inode, io_buf);

    /* 6. 同步新目录 inode */
    memset(io_buf, 0, BLOCK_SIZE * 2);
    inode_sync(cur_part, &new_dir_inode, io_buf);

    /* 7. 同步 inode 位图 */
    bitmap_sync(cur_part, inode_no, INODE_BITMAP);

    dir_close(searched_record.parent_dir);
    sys_free(io_buf);
    return 0;

rollback:
    switch (rollback_step) {
        case 2:
            bitmap_set(&cur_part->block_bitmap,
                       blk2bidx(cur_part, block_no), 0);
            /* fall through */
        case 1:
            bitmap_set(&cur_part->inode_bitmap, inode_no, 0);
            break;
    }
    dir_close(searched_record.parent_dir);
    sys_free(io_buf);
    return -1;
}

/* sys_opendir: 打开目录 */
struct dir* sys_opendir(const char* pathname) {
    ASSERT(strlen(pathname) < MAX_PATH_LEN);

    /* 根目录 */
    if (pathname[0] == '/' && (pathname[1] == '\0' || pathname[1] == '.')) {
        return &root_dir;
    }

    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));
    int32_t inode_no = search_file(pathname, &searched_record);

    struct dir* ret = NULL;
    if (inode_no == -1) {
        printk("In %s, sub path %s not exist\n", pathname, searched_record.searched_path);
    } else {
        if (searched_record.file_type == FT_REGULAR) {
            printk("%s is regular file!\n", pathname);
        } else if (searched_record.file_type == FT_DIRECTORY) {
            ret = dir_open(cur_part, inode_no);
        }
    }
    dir_close(searched_record.parent_dir);
    return ret;
}

/* sys_closedir: 关闭目录 */
int32_t sys_closedir(struct dir* dir) {
    int32_t ret = -1;
    if (dir != NULL) {
        dir_close(dir);
        ret = 0;
    }
    return ret;
}

/* sys_readdir: 读取目录中的下一项 */
struct dir_entry* sys_readdir(struct dir* dir) {
    ASSERT(dir != NULL);
    return dir_read(dir);
}

/* sys_rewinddir: 重置目录读取位置 */
void sys_rewinddir(struct dir* dir) {
    dir->dir_pos = 0;
}

/* sys_rmdir: 删除空目录 */
int32_t sys_rmdir(const char* pathname) {
    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));
    int32_t inode_no = search_file(pathname, &searched_record);
    ASSERT(inode_no != 0);      // 不能删根目录

    int32_t retval = -1;
    if (inode_no == -1) {
        printk("In %s, sub path %s not exist\n", pathname, searched_record.searched_path);
    } else {
        if (searched_record.file_type == FT_REGULAR) {
            printk("%s is regular file!\n", pathname);
        } else {
            struct dir* dir = dir_open(cur_part, inode_no);
            if (!dir_is_empty(dir)) {
                printk("dir %s is not empty, it is not allowed to delete a nonempty directory!\n",
                       pathname);
            } else {
                if (!dir_remove(searched_record.parent_dir, dir)) {
                    retval = 0;
                }
            }
            dir_close(dir);
        }
    }
    dir_close(searched_record.parent_dir);
    return retval;
}

/* sys_getcwd: 获取当前工作目录路径 */
char* sys_getcwd(char* buf, uint32_t size) {
    ASSERT(buf != NULL);
    void* io_buf = sys_malloc(BLOCK_SIZE);
    if (io_buf == NULL) {
        return NULL;
    }

    struct task_struct* cur_thread = running_thread();
    int32_t parent_inode_no = 0;
    int32_t child_inode_no  = cur_thread->cwd_inode_nr;

    /* 如果就是根目录 */
    if (child_inode_no == 0) {
        buf[0] = '/';
        buf[1] = '\0';
        sys_free(io_buf);
        return buf;
    }

    /* 自底向上收集路径名 */
    memset(buf, 0, size);
    char full_path_reverse[MAX_PATH_LEN] = {0};
    while (child_inode_no) {
        struct dir* parent_dir;

        /* 打开当前目录的 inode, 获取 ".." 的 inode 号 */
        parent_dir = dir_open(cur_part, child_inode_no);
        struct dir_entry dir_e;
        search_dir_entry(cur_part, parent_dir, "..", &dir_e);
        parent_inode_no = dir_e.i_no;
        dir_close(parent_dir);

        /* 在父目录中找到 child_inode_no 对应的名字 */
        parent_dir = dir_open(cur_part, parent_inode_no);
        struct dir_entry* de = NULL;
        struct dir* temp_dir = (struct dir*)sys_malloc(sizeof(struct dir));
        temp_dir->inode = parent_dir->inode;
        temp_dir->dir_pos = 0;

        while ((de = dir_read(temp_dir)) != NULL) {
            if (de->i_no == (uint32_t)child_inode_no) {
                strcat(full_path_reverse, "/");
                strcat(full_path_reverse, de->filename);
                break;
            }
        }
        sys_free(temp_dir);
        dir_close(parent_dir);
        child_inode_no = parent_inode_no;
    }

    /* 反转路径: full_path_reverse = "/c/b/a", 需要变成 "/a/b/c" */
    /* 简单方法: 从后向前逐段取出 */
    char* path_ptr = full_path_reverse;
    int32_t len = strlen(full_path_reverse);
    char reversed_buf[MAX_PATH_LEN] = {0};
    int32_t i = len - 1;
    int32_t pos = 0;

    while (i >= 0) {
        /* 找到最后一个 '/' */
        int32_t j = i;
        while (j >= 0 && full_path_reverse[j] != '/') {
            j--;
        }
        /* j 指向 '/', j+1..i 是一个路径分量 */
        reversed_buf[pos++] = '/';
        int32_t k;
        for (k = j + 1; k <= i; k++) {
            reversed_buf[pos++] = full_path_reverse[k];
        }
        i = j - 1;
    }

    if (pos == 0) {
        reversed_buf[0] = '/';
        pos = 1;
    }
    reversed_buf[pos] = '\0';

    if (strlen(reversed_buf) < size) {
        strcpy(buf, reversed_buf);
    } else {
        sys_free(io_buf);
        return NULL;
    }

    sys_free(io_buf);
    return buf;
}

/* sys_chdir: 切换当前工作目录 */
int32_t sys_chdir(const char* pathname) {
    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));
    int32_t inode_no = search_file(pathname, &searched_record);

    int32_t ret = -1;
    if (inode_no != -1) {
        if (searched_record.file_type == FT_DIRECTORY) {
            running_thread()->cwd_inode_nr = inode_no;
            ret = 0;
        } else {
            printk("sys_chdir: %s is regular file or other!\n", pathname);
        }
    }
    dir_close(searched_record.parent_dir);
    return ret;
}

/* sys_stat: 获取文件属性 */
int32_t sys_stat(const char* pathname, struct stat* buf) {
    /* 根目录 */
    if (!strcmp(pathname, "/") || !strcmp(pathname, "/.") || !strcmp(pathname, "/..")) {
        buf->st_filetype = FT_DIRECTORY;
        buf->st_ino = 0;
        buf->st_size = root_dir.inode->i_size;
        return 0;
    }

    int32_t ret = -1;
    struct path_search_record searched_record;
    memset(&searched_record, 0, sizeof(struct path_search_record));
    int32_t inode_no = search_file(pathname, &searched_record);
    if (inode_no != -1) {
        struct inode* obj_inode = inode_open(cur_part, inode_no);
        buf->st_size = obj_inode->i_size;
        inode_close(obj_inode);
        buf->st_filetype = searched_record.file_type;
        buf->st_ino = inode_no;
        ret = 0;
    } else {
        printk("sys_stat: %s not found\n", pathname);
    }
    dir_close(searched_record.parent_dir);
    return ret;
}
