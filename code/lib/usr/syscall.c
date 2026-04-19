#include "syscall.h"
#include "stdarg.h"

/* 通用系统调用接口
 * 
 * 参数通过寄存器传递：
 *   eax: 系统调用号
 *   ebx, ecx, edx, esi, edi: 最多 5 个参数
 * 
 * 通过 int $0x80 触发系统调用，内核处理后返回值存入 eax
 */
int syscall(uint32_t number, ...) {
    va_list args;
    va_start(args, number);
    
    /* 提取参数（即使用不到也要取出来，内核会忽略多余的）*/
    uint32_t arg1 = va_arg(args, uint32_t);     // 获取下一个参数,并移动指针到下一个参数位置
    uint32_t arg2 = va_arg(args, uint32_t);
    uint32_t arg3 = va_arg(args, uint32_t);
    uint32_t arg4 = va_arg(args, uint32_t);
    uint32_t arg5 = va_arg(args, uint32_t);
    
    va_end(args);
    
    int retval;
    asm volatile (
        "int $0x80"
        : "=a" (retval)
        : "a" (number), "b" (arg1), "c" (arg2), 
          "d" (arg3), "S" (arg4), "D" (arg5)
        : "memory"
    );
    
    return retval;
}

/* 系统调用封装 - 提供类型安全和易用性 */

uint32_t getpid(void) {
    return syscall(SYS_GETPID);
}

int write(int fd, const void* buf, uint32_t count) {
    return syscall(SYS_WRITE, fd, buf, count);
}

void* malloc(uint32_t size) {
    return (void*)syscall(SYS_MALLOC, size);
}

void free(void* ptr) {
    syscall(SYS_FREE, ptr);
}

int open(const char* pathname, uint8_t flags) {
    return syscall(SYS_OPEN, pathname, flags);
}

int close(int fd) {
    return syscall(SYS_CLOSE, fd);
}

int read(int fd, void* buf, uint32_t count) {
    return syscall(SYS_READ, fd, buf, count);
}

int lseek(int fd, int32_t offset, uint8_t whence) {
    return syscall(SYS_LSEEK, fd, offset, whence);
}

int unlink(const char* pathname) {
    return syscall(SYS_UNLINK, pathname);
}

int mkdir(const char* pathname) {
    return syscall(SYS_MKDIR, pathname);
}

void* opendir(const char* pathname) {
    return (void*)syscall(SYS_OPENDIR, pathname);
}

int closedir(void* dir) {
    return syscall(SYS_CLOSEDIR, dir);
}

void* readdir(void* dir) {
    return (void*)syscall(SYS_READDIR, dir);
}

void rewinddir(void* dir) {
    syscall(SYS_REWINDDIR, dir);
}

int rmdir(const char* pathname) {
    return syscall(SYS_RMDIR, pathname);
}

char* getcwd(char* buf, uint32_t size) {
    return (char*)syscall(SYS_GETCWD, buf, size);
}

int chdir(const char* pathname) {
    return syscall(SYS_CHDIR, pathname);
}

int stat(const char* pathname, void* buf) {
    return syscall(SYS_STAT, pathname, buf);
}
