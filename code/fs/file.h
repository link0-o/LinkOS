#ifndef __FS_FILE_H
#define __FS_FILE_H

#include "stdint.h"
#include "global.h"
#include "inode.h"
#include "dir.h"
#include "ide.h"

/* 文件操作标志 */
enum oflags {
    O_RDONLY,       // 只读
    O_WRONLY,       // 只写
    O_RDWR,         // 读写
    O_CREAT = 4     // 创建（可与读写标志组合使用）
};

/* seek 基准 */
enum whence {
    SEEK_SET = 1,   // 从文件开头
    SEEK_CUR,       // 从当前位置
    SEEK_END        // 从文件末尾
};

/* 文件结构（全局打开文件表中的一项） */
struct file {
    uint32_t fd_pos;            // 当前读写位置
    uint32_t fd_flag;           // 文件操作标志（oflags）
    struct inode* fd_inode;     // 指向的 inode
};

/* 文件属性信息（供 sys_stat 使用） */
struct stat {
    uint32_t st_ino;            // inode 号
    uint32_t st_size;           // 文件大小
    enum file_types st_filetype; // 文件类型
};

/* 标准文件描述符 */
#define stdin_no  0
#define stdout_no 1
#define stderr_no 2

/* 全局打开文件表 */
#define MAX_FILE_OPEN 512
extern struct file file_table[MAX_FILE_OPEN];

/* 文件操作函数 */

/* 从全局文件表获取一个空闲槽，返回下标，失败返回 -1 */
int32_t get_free_slot_in_global(void);

/* 将全局文件表下标 globa_fd_idx 安装到当前进程的 fd_table 中
 * 返回进程局部描述符编号，失败返回 -1 */
int32_t pcb_fd_install(int32_t globa_fd_idx);

/* 从 inode 位图中分配一个空闲 inode，返回 inode 号 */
int32_t inode_bitmap_alloc(struct partition* part);

/* 从块位图中分配一个空闲块，返回块号（扇区号） */
int32_t block_bitmap_alloc(struct partition* part);

/* 将位图第 bit_idx 位所在扇区同步回磁盘
 * btmp_type: BLOCK_BITMAP 或 INODE_BITMAP */
void bitmap_sync(struct partition* part, uint32_t bit_idx, uint8_t btmp_type);

/* 创建文件，成功返回文件描述符，失败返回 -1 */
int32_t file_create(struct dir* parent_dir, char* filename, uint8_t flag);

/* 打开 inode_no 对应的文件，成功返回文件描述符，失败返回 -1 */
int32_t file_open(uint32_t inode_no, uint8_t flag);

/* 关闭文件 */
int32_t file_close(struct file* file);

/* 写文件，返回写入的字节数，失败返回 -1 */
int32_t file_write(struct file* file, const void* buf, uint32_t count);

/* 读文件，返回读到的字节数，-1 表示失败，0 表示到达文件末尾 */
int32_t file_read(struct file* file, void* buf, uint32_t count);

/* 位图类型标识（用于 bitmap_sync） */
#define BLOCK_BITMAP 0
#define INODE_BITMAP 1

#endif
