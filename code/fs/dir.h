#ifndef __FS_DIR_H
#define __FS_DIR_H

#include "stdint.h"
#include "global.h"
#include "inode.h"
#include "ide.h"

#define MAX_FILE_NAME_LEN 16    // 文件名最大长度（含 '\0'）

/* 目录结构（在内存中, 运行时，用于遍历目录） */
struct dir {
    struct inode* inode;        // 目录对应的 inode
    uint32_t dir_pos;           // 记录在目录中的读取偏移（用于 readdir）
    uint8_t  dir_buf[BLOCK_SIZE]; // 目录数据缓冲（一个块 = 4KB）
};

/* 文件类型枚举 */
enum file_types {
    FT_UNKNOWN,     // 未知类型
    FT_REGULAR,     // 普通文件
    FT_DIRECTORY    // 目录
};

/* 目录项结构（24 字节）, 在磁盘中存储，表示目录中的一个文件或子目录
 * 每块可存放 4096 / 24 = 170 个目录项 */
struct dir_entry {
    char filename[MAX_FILE_NAME_LEN];   // 文件名（16 字节）
    uint32_t i_no;                      // inode 编号（4 字节）
    enum file_types f_type;             // 文件类型（4 字节, enum = int）
};

/* 全局根目录 */
extern struct dir root_dir;

/* 目录操作函数 */

/* 打开根目录 */
void open_root_dir(struct partition* part);

/* 打开 inode_no 对应的目录 */
struct dir* dir_open(struct partition* part, uint32_t inode_no);

/* 关闭目录（根目录不释放） */
void dir_close(struct dir* dir);

/* 在目录 pdir 中查找 name，找到则将目录项写入 dir_e，返回 true */
bool search_dir_entry(struct partition* part, struct dir* pdir,
                      const char* name, struct dir_entry* dir_e);

/* 用前面参数填充目录项结构 */
void create_dir_entry(char* filename, uint32_t inode_no,
                      uint8_t file_type, struct dir_entry* p_de);

/* 将目录项 p_de 写入目录 parent_dir（必要时分配新的数据块） */
bool sync_dir_entry(struct dir* parent_dir, struct dir_entry* p_de, void* io_buf);

/* 从目录 pdir 中删除 inode_no 对应的目录项 */
bool delete_dir_entry(struct partition* part, struct dir* pdir,
                      uint32_t inode_no, void* io_buf);

/* 读取目录中的下一个目录项, 返回 NULL 表示读完 */
struct dir_entry* dir_read(struct dir* dir);

/* 判断目录是否为空（只有 "." 和 ".."） */
bool dir_is_empty(struct dir* dir);

/* 在 parent_dir 中删除子目录 child_dir（需要为空目录） */
int32_t dir_remove(struct dir* parent_dir, struct dir* child_dir);

#endif
