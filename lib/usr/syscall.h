#ifndef __LIB_USER_SYSCALL_H
#define __LIB_USER_SYSCALL_H

#include "stdint.h"

/* 系统调用号定义
 * 每个系统调用对应一个唯一编号，内核根据编号执行对应功能 */
enum SYSCALL_NR {
    SYS_GETPID = 0,      // 获取进程 ID
    SYS_WRITE,           // 写文件/终端
    SYS_MALLOC,          // 申请内存
    SYS_FREE,            // 释放内存
};

/* 通用系统调用接口（仿照 Linux glibc）
 * 使用可变参数，最多支持 5 个参数 */
int syscall(uint32_t number, ...);

/* 系统调用封装 - 提供类型检查和更好的可读性 */
uint32_t getpid(void);
int write(int fd, const void* buf, uint32_t count);
void* malloc(uint32_t size);
void free(void* ptr);

#endif
