#ifndef __KERNEL_SYSCALL_H
#define __KERNEL_SYSCALL_H

#include "stdint.h"

/* 系统调用号（与 lib/usr/syscall.h 中的定义保持一致）*/
enum SYSCALL_NR {
    SYS_GETPID = 0,
    SYS_WRITE,
    SYS_MALLOC,
    SYS_FREE,
};

/* 系统调用实现函数 */
uint32_t sys_getpid(void);
int sys_write(int fd, const void* buf, uint32_t count);
void* sys_malloc(uint32_t size);
void sys_free(void* ptr);

#endif
