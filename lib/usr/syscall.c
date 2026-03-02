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
