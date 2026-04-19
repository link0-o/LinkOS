#ifndef __KERNEL_SYSCALL_H
#define __KERNEL_SYSCALL_H

#include "stdint.h"

/* 系统调用号（与 lib/usr/syscall.h 中的定义保持一致）*/
enum SYSCALL_NR {
    SYS_GETPID = 0,
    SYS_WRITE,
    SYS_MALLOC,
    SYS_FREE,
    SYS_OPEN,
    SYS_CLOSE,
    SYS_READ,
    SYS_LSEEK,
    SYS_UNLINK,
    SYS_MKDIR,
    SYS_OPENDIR,
    SYS_CLOSEDIR,
    SYS_READDIR,
    SYS_REWINDDIR,
    SYS_RMDIR,
    SYS_GETCWD,
    SYS_CHDIR,
    SYS_STAT,
};

/* 系统调用实现函数 */
uint32_t sys_getpid(void);
void* sys_malloc(uint32_t size);
void sys_free(void* ptr);

/* 注意: sys_write/sys_read/sys_open 等文件系统相关系统调用在 fs/fs.h 中声明 */

#endif
