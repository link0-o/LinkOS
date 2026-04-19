#ifndef __FS_FS_H
#define __FS_FS_H

#include "stdint.h"
#include "ide.h"
#include "dir.h"
#include "file.h"
#include "super_block.h"

/* 文件系统常量 */
#define FS_MAGIC           0x19590321   // 文件系统魔数 (v4: 4KB块 + 三级间接 + 分区相对块号)
#define MAX_FILES_PER_PART 4096         // 每个分区最大文件/inode 数
#define BITS_PER_BLOCK     (BLOCK_SIZE * 8)  // 每块的位数 (4096 × 8 = 32768)
#define MAX_PATH_LEN       512          // 最大路径长度

/* ===== 块号转换辅助函数 (仿照 Linux ext2) =====
 *
 * inode 和间接块中存储的是 **分区相对块号** (partition-relative block number):
 *   block_no = 该扇区相对于分区起始的偏移量
 *   0 表示 "未分配" (block 0 = 引导扇区, 永远不作为数据块)
 *
 * block_bitmap 的 bit N 对应数据区的第 N 个块, 其分区相对块号 = data_start + N
 */

/* 分区相对块号 → 绝对 LBA (用于 ide_read/ide_write) */
static inline uint32_t blk2lba(struct partition* part, uint32_t block_no) {
    return part->start_lba + block_no * SECTORS_PER_BLOCK;      // 一个块 4kb = 8 个扇区
}

/* block_bitmap 位索引 → 分区相对块号 */
static inline uint32_t bidx2blk(struct partition* part, uint32_t bit_idx) {
    return part->sb->data_start + bit_idx;
}

/* 分区相对块号 → block_bitmap 位索引 */
static inline uint32_t blk2bidx(struct partition* part, uint32_t block_no) {
    return block_no - part->sb->data_start;
}

/* 当前挂载的分区 */
extern struct partition* cur_part;

/* 路径搜索记录（用于 search_file 返回搜索结果） */
struct path_search_record {
    char searched_path[MAX_PATH_LEN]; // 已经搜索到的路径（父路径）
    struct dir* parent_dir;           // 搜索到的最深层父目录
    enum file_types file_type;        // 找到的条目的文件类型
};

/* 文件系统初始化（格式化 + 挂载） */
void filesys_init(void);

/* ===== 系统调用实现 ===== */

/* 打开或创建文件, 返回文件描述符，失败返回 -1
 * flags: O_RDONLY, O_WRONLY, O_RDWR, O_CREAT（可组合）*/
int32_t sys_open(const char* pathname, uint8_t flags);

/* 关闭文件描述符, 成功返回 0, 失败返回 -1 */
int32_t sys_close(int32_t fd);

/* 写文件，fd=1/2 写控制台，其余写文件，返回字节数或 -1 */
int32_t sys_write(int32_t fd, const void* buf, uint32_t count);

/* 读文件，返回读取字节数，0 表示 EOF，-1 表示失败 */
int32_t sys_read(int32_t fd, void* buf, uint32_t count);

/* 移动文件读写位置，返回新的偏移量，失败返回 -1 */
int32_t sys_lseek(int32_t fd, int32_t offset, uint8_t whence);

/* 删除文件（非目录），成功返回 0，失败返回 -1 */
int32_t sys_unlink(const char* pathname);

/* 创建目录，成功返回 0，失败返回 -1 */
int32_t sys_mkdir(const char* pathname);

/* 打开目录，返回目录指针，失败返回 NULL */
struct dir* sys_opendir(const char* pathname);

/* 关闭目录，成功返回 0，失败返回 -1 */
int32_t sys_closedir(struct dir* dir);

/* 读取目录中的下一个条目，返回 dir_entry 指针，读完返回 NULL */
struct dir_entry* sys_readdir(struct dir* dir);

/* 重置目录读取位置到开头 */
void sys_rewinddir(struct dir* dir);

/* 删除空目录，成功返回 0，失败返回 -1 */
int32_t sys_rmdir(const char* pathname);

/* 获取当前工作目录路径，写入 buf，返回 buf 指针，失败返回 NULL */
char* sys_getcwd(char* buf, uint32_t size);

/* 切换当前工作目录，成功返回 0，失败返回 -1 */
int32_t sys_chdir(const char* pathname);

/* 获取文件属性，成功返回 0，失败返回 -1 */
int32_t sys_stat(const char* pathname, struct stat* buf);

/* ===== 内部辅助函数 ===== */

/* 将进程局部的 fd 转换为全局文件表下标 */
uint32_t fd_local2global(uint32_t local_fd);

/* 解析路径: 从 pathname 中取出最顶层的文件名写入 name_store
 * 返回剩余子路径指针 */
char* path_parse(char* pathname, char* name_store);

/* 计算路径深度 (如 "/a/b/c" 深度为 3) */
int32_t path_depth_cnt(char* pathname);

/* 搜索文件, 找到返回 inode 号, 否则返回 -1
 * searched_record 记录搜索过程（父目录等信息）*/
int32_t search_file(const char* pathname, struct path_search_record* searched_record);

#endif
