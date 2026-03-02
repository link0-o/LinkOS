#include "print.h"
#include "init.h"
#include "memory.h"
#include "thread.h"
#include "sched.h"
#include "interrupt.h"
#include "process.h"
#include "console.h"
#include "syscall.h"
#include "stdio.h"
#include "string.h"

void k_thread_a(void* arg);
void k_thread_b(void* arg);
void u_prog_a(void);
void u_prog_b(void);

int main(void) {
    put_str("I am kernel\n");
    init_all();

    /* ---------- 内核线程 + 用户进程完整测试 ---------- */
    put_str("creating threads...\n");
    thread_start("thread_a", 10, k_thread_a, "A");
    put_str("thread_a created\n");
    thread_start("thread_b", 20, k_thread_b, "B");
    put_str("thread_b created\n");
    process_execute(u_prog_a, "user_prog_a");
    put_str("user_prog_a created\n");
    process_execute(u_prog_b, "user_prog_b");
    put_str("user_prog_b created\n");

    intr_enable();
    while(1);
    return 0;
}

/* 内核线程A：循环 malloc/free 小内存，测试并发安全 */
void k_thread_a(void* arg) {
    uint32_t pid = sys_getpid();
    uint32_t i;
    for (i = 0; i < 50; i++) {
        void* p = sys_malloc(16);
        if (p != NULL) {
            sys_free(p);
        }
    }
    console_put_str("kA: 50x malloc/free(16) ok, pid=");
    console_put_int(pid);
    console_put_char('\n');

    /* 测试 7 种规格 */
    uint32_t sizes[] = {16, 32, 64, 128, 256, 512, 1024};
    for (i = 0; i < 7; i++) {
        void* p = sys_malloc(sizes[i]);
        if (p != NULL) {
            sys_free(p);
        } else {
            console_put_str("kA: fail size=");
            console_put_int(sizes[i]);
            console_put_char('\n');
        }
    }
    console_put_str("kA: all 7 sizes ok\n");
    while (1);
}

/* 内核线程B：循环 malloc/free 不同大小，与线程A并发 */
void k_thread_b(void* arg) {
    uint32_t pid = sys_getpid();
    uint32_t i;
    for (i = 0; i < 50; i++) {
        void* p = sys_malloc(64);
        if (p != NULL) {
            sys_free(p);
        }
    }
    console_put_str("kB: 50x malloc/free(64) ok, pid=");
    console_put_int(pid);
    console_put_char('\n');

    /* 测试大内存 */
    void* big = sys_malloc(2048);
    if (big != NULL) {
        sys_free(big);
        console_put_str("kB: big malloc/free(2048) ok\n");
    }
    while (1);
}

/* 用户进程A：malloc + free + write */
void u_prog_a(void) {
    /* 用户态 malloc(64): syscall 2 */
    void* ptr;
    asm volatile (
        "movl $2, %%eax\n\t"
        "movl $64, %%ebx\n\t"
        "int $0x80\n\t"
        "movl %%eax, %0"
        : "=r" (ptr)
        :
        : "eax", "ebx"
    );

    if (ptr != (void*)0) {
        /* 用户态 free: syscall 3 */
        asm volatile (
            "movl $3, %%eax\n\t"
            "movl %0, %%ebx\n\t"
            "int $0x80"
            :
            : "r" (ptr)
            : "eax", "ebx"
        );
    }

    char msg[] = "uA: ok\n";
    asm volatile (
        "movl $1, %%eax\n\t"
        "movl $1, %%ebx\n\t"
        "movl %0, %%ecx\n\t"
        "movl $7, %%edx\n\t"
        "int $0x80"
        :
        : "r" (msg)
        : "eax", "ebx", "ecx", "edx"
    );

    while (1);
}

/* 用户进程B：malloc + free + write */
void u_prog_b(void) {
    /* 用户态 malloc(128): syscall 2 */
    void* ptr;
    asm volatile (
        "movl $2, %%eax\n\t"
        "movl $128, %%ebx\n\t"
        "int $0x80\n\t"
        "movl %%eax, %0"
        : "=r" (ptr)
        :
        : "eax", "ebx"
    );

    if (ptr != (void*)0) {
        /* 用户态 free: syscall 3 */
        asm volatile (
            "movl $3, %%eax\n\t"
            "movl %0, %%ebx\n\t"
            "int $0x80"
            :
            : "r" (ptr)
            : "eax", "ebx"
        );
    }

    char msg[] = "uB: ok\n";
    asm volatile (
        "movl $1, %%eax\n\t"
        "movl $1, %%ebx\n\t"
        "movl %0, %%ecx\n\t"
        "movl $7, %%edx\n\t"
        "int $0x80"
        :
        : "r" (msg)
        : "eax", "ebx", "ecx", "edx"
    );

    while (1);
}
