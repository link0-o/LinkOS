#include "syscall.h"
#include "stdint.h"
#include "print.h"
#include "thread.h"
#include "string.h"
#include "memory.h"
#include "console.h"
#include "list.h"

#define PG_SIZE 4096

/* pool 结构在 memory.c 中定义，这里声明外部引用 */
extern struct pool kernel_pool, user_pool;

/* 系统调用实现 */

/* 获取当前进程 PID */
uint32_t sys_getpid(void) {
    return running_thread()->pid;
}

/* 写入数据（简化版，只支持输出到控制台）*/
int sys_write(int fd, const void* buf, uint32_t count) {
    if (fd == 1 || fd == 2) {  // stdout 或 stderr
        const char* str = buf;
        console_put_str(str);
        return strlen(str);
    }
    return -1;
}

/*
 * sys_malloc
 *
 * 一、linux glibc malloc 原理（参考）
 *
 *   glibc 用"分级分桶"管理从内核拿来的内存，核心是三种 bin：
 *
 *   Fastbin：16~160 字节的小块（32位），不合并，链表直接取用，最快。
 *   Small bin：16~1008 字节，和 Fastbin 重叠但会合并相邻空闲块减少碎片。
 *   Large bin：>1008 字节，按范围分桶，支持最佳适配。
 *   >128KB：直接 mmap 独立映射，free 时立即归还内核。
 *
 *   内存来源：向内核调 brk（移动堆顶）或 mmap 拿大块内存，
 *             然后在用户态自己切割管理，尽量减少陷入内核的次数。
 *
 * 二、linux 内核态 kmalloc 原理
 *
 *   伙伴系统（Buddy）：按 2 的幂次管理物理页（4KB/8KB/16KB...），
 *                      释放时合并相邻空闲块。
 *                      对应我们的 get_kernel_pages / get_user_pages。
 *
 *   Slab 分配器：从伙伴系统拿页后拆成固定大小的对象缓存，
 *                专门给 task_struct、file 等固定大小内核对象用。
 *                和我们 arena 拆成 mem_block 的思路一致。
 *
 * 三、我们的实现目标（简化版）
 *
 *   小内存（<= 1024B）：7 种固定规格（16/32/64/128/256/512/1024B），
 *                       每种规格一个 free_list，不够时申请新页做新 arena。
 *   大内存（> 1024B）：直接整页分配，arena 头部记录页数，free 时归还。
 *
 *   Arena 结构：从内核申请一页（4KB），头部存 arena 元数据，
 *               剩余空间按规格切成 mem_block 用链表管理。
 *
 * 内存结构图：
 *
 *   k_block_descs[7]
 *   ┌──────────────────────────────────────────────────────┐
 *   │ desc[0] 16B   free_list ──→ [blk]→[blk]→[blk]→...  │
 *   │ desc[1] 32B   free_list ──→ [blk]→[blk]→...         │
 *   │ desc[2] 64B   free_list ──→ (空)                     │
 *   │ ...                                                  │
 *   └────────────┬─────────────────────┬────────────────────┘
 *                │ free_list 跨页串联  │
 *                ↓                     ↓
 *   物理页A（32B规格）         物理页B（32B规格）
 *   ┌────────────────────┐    ┌────────────────────┐
 *   │arena               │    │arena               │
 *   │ desc ──→ desc[1]   │    │ desc ──→ desc[1]   │
 *   │ cnt = 3            │    │ cnt = 126          │
 *   ├────────────────────┤    ├────────────────────┤
 *   │ blk0 (已分配)      │    │ blk0 ←→ free_list  │
 *   │ blk1 ←→ free_list  │    │ blk1 ←→ free_list  │
 *   │ blk2 (已分配)      │    │ ...                │
 *   │ blk3 ←→ free_list  │    │ blk125←→ free_list │
 *   │ blk4 ←→ free_list  │    └────────────────────┘
 *   │ blk5 ←→ free_list  │
 *   └────────────────────┘
 *
 *   大内存页（size > 1024）：
 *   ┌────────────────────┐
 *   │arena               │
 *   │ desc = NULL        │ ← NULL 表示大内存
 *   │ cnt = 3（占3页）   │
 *   ├────────────────────┤
 *   │  可用内存          │ ← malloc 返回这里
 *   │  （跨3页）         │
 *   └────────────────────┘
 */
void* sys_malloc(uint32_t size) {
    if (size == 0) return NULL;

    struct mem_block_desc* descs;
    enum pool_flags pf;
    struct pool* mem_pool;

    /* 判断当前是内核线程还是用户进程 */
    struct task_struct* cur = running_thread();
    if (cur->pgdir == NULL) {
        /* 内核线程：使用全局 k_block_descs */
        pf = PF_KERNEL;
        mem_pool = &kernel_pool;
        descs = k_block_descs;
    } else {
        /* 用户进程：使用进程自己的 u_block_descs */
        pf = PF_USER;
        mem_pool = &user_pool;
        descs = cur->u_block_descs;
    }

    mem_lock_acquire(pf);

    /* 大内存：直接按页分配 */
    if (size > 1024) {
        uint32_t pg_cnt = (size + sizeof(struct arena) + PG_SIZE - 1) / PG_SIZE;
        struct arena* a = malloc_page(pf, pg_cnt);
        if (a == NULL) {
            mem_lock_release(pf);
            return NULL;
        }
        memset(a, 0, pg_cnt * PG_SIZE);
        a->desc  = NULL;
        a->cnt   = pg_cnt;
        mem_lock_release(pf);
        return (void*)(a + 1);   /* 跳过 arena 头，返回可用内存 */
    }

    /* 小内存：找对应规格 */
    uint32_t i;
    for (i = 0; i < MEM_BLOCK_DESC_CNT; i++) {
        if (size <= descs[i].block_size) break;
    }

    struct mem_block_desc* desc = &descs[i];

    /* free_list 为空，申请新页初始化为 arena */
    if (list_empty(&desc->free_list)) {
        struct arena* a = malloc_page(pf, 1);
        if (a == NULL) {
            mem_lock_release(pf);
            return NULL;
        }

        memset(a, 0, PG_SIZE);
        a->desc  = desc;
        a->cnt   = desc->blocks_per_arena;

        /* 把所有 mem_block 切出来挂到 free_list */
        uint32_t b;
        for (b = 0; b < desc->blocks_per_arena; b++) {
            struct mem_block* blk = (struct mem_block*)((uint32_t)(a + 1) + b * desc->block_size);
            list_append(&desc->free_list, &blk->free_elem);
        }
    }

    /* 从 free_list 摘一块 */
    struct mem_block* blk =
        elem2entry(struct mem_block, free_elem, list_pop(&desc->free_list));        // 拿到空闲地址的指针

    /* 反推所在 arena，减少 cnt */
    struct arena* a = (struct arena*)((uint32_t)blk & 0xfffff000);
    a->cnt--;

    mem_lock_release(pf);
    memset(blk, 0, desc->block_size);
    return (void*)blk;
}

void sys_free(void* ptr) {
    if (ptr == NULL) return;

    struct arena* a = (struct arena*)((uint32_t)ptr & 0xfffff000);
    enum pool_flags pf = (running_thread()->pgdir == NULL) ? PF_KERNEL : PF_USER;

    mem_lock_acquire(pf);

    if (a->desc == NULL) {
        /* 大内存：直接归还所有页 */
        mfree_page(pf, a, a->cnt);
    } else {
        /* 小内存：插回 free_list */
        struct mem_block* blk = (struct mem_block*)ptr;
        list_append(&a->desc->free_list, &blk->free_elem);
        a->cnt++;

        /* 整页都空了 → 把该页所有块从 free_list 摘掉，然后还页 */
        if (a->cnt == a->desc->blocks_per_arena) {
            uint32_t b;
            for (b = 0; b < a->desc->blocks_per_arena; b++) {
                struct mem_block* blk_to_rm =
                    (struct mem_block*)((uint32_t)(a + 1) + b * a->desc->block_size);
                list_remove(&blk_to_rm->free_elem);
            }
            mfree_page(pf, a, 1);
        }
    }

    mem_lock_release(pf);
}
 
/* 系统调用表 - 根据系统调用号索引对应的处理函数 */
typedef void* syscall_func;
syscall_func syscall_table[] = {
    sys_getpid,
    sys_write,
    sys_malloc,
    sys_free
};

/* 系统调用表大小 */
uint32_t syscall_nr = sizeof(syscall_table) / sizeof(syscall_func);
