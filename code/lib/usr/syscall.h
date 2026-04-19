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
    SYS_OPEN,            // 打开文件
    SYS_CLOSE,           // 关闭文件
    SYS_READ,            // 读文件
    SYS_LSEEK,           // 移动文件指针
    SYS_UNLINK,          // 删除文件
    SYS_MKDIR,           // 创建目录
    SYS_OPENDIR,         // 打开目录
    SYS_CLOSEDIR,        // 关闭目录
    SYS_READDIR,         // 读取目录项
    SYS_REWINDDIR,       // 重置目录读取位置
    SYS_RMDIR,           // 删除目录
    SYS_GETCWD,          // 获取当前工作目录
    SYS_CHDIR,           // 切换工作目录
    SYS_STAT,            // 获取文件属性
};

/* 通用系统调用接口（仿照 Linux glibc）
 * 使用可变参数，最多支持 5 个参数 */
int syscall(uint32_t number, ...);

/* 系统调用封装 - 提供类型检查和更好的可读性 */
uint32_t getpid(void);
int write(int fd, const void* buf, uint32_t count);
void* malloc(uint32_t size);
void free(void* ptr);
int open(const char* pathname, uint8_t flags);
int close(int fd);
int read(int fd, void* buf, uint32_t count);
int lseek(int fd, int32_t offset, uint8_t whence);
int unlink(const char* pathname);
int mkdir(const char* pathname);
void* opendir(const char* pathname);
int closedir(void* dir);
void* readdir(void* dir);
void rewinddir(void* dir);
int rmdir(const char* pathname);
char* getcwd(char* buf, uint32_t size);
int chdir(const char* pathname);
int stat(const char* pathname, void* buf);

#endif
