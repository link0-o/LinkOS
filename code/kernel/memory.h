#ifndef __KERNEL_MEMORY_H
#define __KERNEL_MEMORY_H
#include "stdint.h"
#include "bitmap.h"
#include "list.h"

/* 7 种固定规格：16/32/64/128/256/512/1024 字节 */
#define MEM_BLOCK_DESC_CNT 7

/* 内存块（空闲时挂在 free_list 上，分配后整块给用户）*/
struct mem_block {
    struct list_elem free_elem;
};

/* 内存块描述符：描述某种规格的内存块 */
struct mem_block_desc {
    uint32_t block_size;        // 该规格的块大小
    uint32_t blocks_per_arena;  // 一个 arena 能切出多少块
    List     free_list;         // 该规格所有空闲块的链表
};

/* arena：一页内存的头部元数据
 * desc == NULL 表示大内存（直接按页分配），否则为小内存分块 */
struct arena {
    struct mem_block_desc* desc; // 规格描述符，大内存时为 NULL
    uint32_t cnt;                // 小内存：剩余空闲块数；大内存：占用页数
};

/* 虚拟地址池，用于虚拟地址管理 */
struct virtual_addr {
    Bitmap vaddr_bitmap;           // 虚拟地址用到的位图结构
    uint32_t vaddr_start;          // 虚拟地址起始地址
};

extern struct pool kernel_pool, user_pool;

/* 内存池标记，用于判断用哪个内存池 */
enum pool_flags {
    PF_KERNEL = 1,      // 内核内存池
    PF_USER   = 2       // 用户内存池
};

#define PG_P_0  0       // 页表项或页目录项不存在属性位
#define PG_P_1  1       // 页表项或页目录项存在属性位
#define PG_RW_R 0       // R/W 属性位值，读/执行
#define PG_RW_W 2       // R/W 属性位值，读/写/执行
#define PG_US_S 0       // U/S 属性位值，系统级
#define PG_US_U 4       // U/S 属性位值，用户级

/* 内存管理函数声明 */
void mem_init(void);
void* get_kernel_pages(uint32_t pg_cnt);
void* get_user_pages(uint32_t pg_cnt);
void* get_a_page(enum pool_flags pf, uint32_t vaddr);
uint32_t addr_v2p(uint32_t vaddr);
void* malloc_page(enum pool_flags pf, uint32_t pg_cnt);
void  mfree_page(enum pool_flags pf, void* _vaddr, uint32_t pg_cnt);
uint32_t* pte_ptr(uint32_t vaddr);
uint32_t* pde_ptr(uint32_t vaddr);

/* 内存池锁的获取与释放（struct pool 定义在 memory.c，无法直接访问） */
void mem_lock_acquire(enum pool_flags pf);
void mem_lock_release(enum pool_flags pf);

/* 内核/用户的规格描述符数组，供 sys_malloc/sys_free 使用 */
extern struct mem_block_desc k_block_descs[MEM_BLOCK_DESC_CNT];

/* 初始化内存块描述符数组（供用户进程初始化 u_block_descs 使用）*/
void block_desc_init(struct mem_block_desc* desc_array);

/* 内核页目录 PDE[768..1022] 的缓存副本，共 255 项。
 * 当 page_table_add 为内核虚拟地址创建新 PDE 时会更新此缓存。
 * 用户进程切换时从此缓存同步内核 PDE，避免缺页。 */
extern uint32_t kernel_pde_cache[255];

/* 内核页目录的虚拟地址（固定映射到物理 0x100000）*/
#define KERNEL_PAGE_DIR  ((uint32_t*)0xc0100000)

#endif