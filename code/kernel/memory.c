#include "memory.h"
#include "stdint.h"
#include "print.h"
#include "debug.h"
#include "string.h"
#include "sync.h"
#include "thread.h"

#define PG_SIZE 4096

void block_desc_init(struct mem_block_desc* desc_array);     // 初始化内存块描述符数组

/************************ 位图地址 *****************************
 * 因为 0xc009f000 是内核主线程栈顶，0xc009e000 是内核主线程的 pcb。
 * 一个页框大小的位图可表示 128MB 内存，位图位置安排在地址 0xc009a000
 * 这样本系统最大支持 4 个页框的位图，即 512MB */

 #define MEM_BITMAP_BASE 0xc009a000
 /******************************************************************/

 /* 0xc0000000 是内核从虚拟地址 3G 起 0x100000 意指跨过低端 1MB 内存，使虚拟地址在逻辑上连续 */
#define K_HEAP_START 0xc0100000

//0xffc00000 = 1111 1111 1100 0000 0000 0000 0000 0000
#define PDE_IDX(addr) ((addr & 0xffc00000) >> 22)           //保留高10位,清零低22位
#define PTE_IDX(addr) ((addr & 0x003ff000) >> 12)           //保留中间10位,清零其他位

struct pool{
    Bitmap pool_bitmap;          // 本内存池用到的位图结构
    uint32_t phy_addr_start;     // 本内存池所管理的物理内存起始地址
    uint32_t pool_size;          // 容量
    struct lock lock;            // 锁
};

struct pool kernel_pool, user_pool;  // 内核内存池和用户内存池
struct virtual_addr kernel_vaddr;    // 内核虚拟地址池

/* 内核页目录 PDE[768..1022] 的缓存副本 */
uint32_t kernel_pde_cache[255];

/* 函数前向声明 */
static void* palloc(struct pool* m_pool);

/* 在 pf 表示的虚拟内存池中申请 pg_cnt 个虚拟页，成功则返回虚拟页的起始地址，失败则返回 NULL */
static void* vaddr_get(enum pool_flags pf, uint32_t pg_cnt){
    int vaddr_start = 0, bit_idx_start = -1;
    uint32_t cnt = 0;
    if(pf == PF_KERNEL){
        bit_idx_start = bitmap_scan(&kernel_vaddr.vaddr_bitmap, pg_cnt);
        if (bit_idx_start == -1)    return NULL;            // 申请失败

        while (cnt < pg_cnt) {
            bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 1);
        }
        vaddr_start = kernel_vaddr.vaddr_start + bit_idx_start * PG_SIZE;
    }else{
        // 用户内存池
        struct task_struct* cur = running_thread();
        bit_idx_start = bitmap_scan(&cur->userprog_vaddr.vaddr_bitmap, pg_cnt);
        if (bit_idx_start == -1) {
            return NULL;
        }

        while(cnt < pg_cnt) {
            bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 1);
        }
        vaddr_start = cur->userprog_vaddr.vaddr_start + bit_idx_start * PG_SIZE;

        /* (0xc0000000 - PG_SIZE)作为用户 3 级栈已经在 start_process 被分配 */
        ASSERT((uint32_t)vaddr_start < (0xc0000000 - PG_SIZE));
    }
    return (void*)vaddr_start;
}

/* 得到虚拟地址 vaddr 对应的 pte 指针 */
uint32_t* pte_ptr(uint32_t vaddr){
    /* 利用页表自映射机制,通过虚拟地址访问页表项(PTE)
     * 原理: 访问 0xFFC00000+ 时,CPU会把页目录当作"页表"来查找
     * 0xffc00000: 自映射基址,PDE[1023]→页目录,再把PDE当PTE查找
     * ((vaddr & 0xffc00000) >> 10): vaddr对应的页表在自映射区的偏移(PDE索引×4096)
     * PTE_IDX(vaddr) * 4: 页表内的PTE偏移(PTE索引×4字节)
     */
    uint32_t* pte = (uint32_t*)(0xffc00000 + ((vaddr & 0xffc00000) >> 10) + PTE_IDX(vaddr) * 4);        //*2==<<2
    return pte;
}

/* 得到虚拟地址 vaddr 对应的 pde 指针 */
uint32_t* pde_ptr(uint32_t vaddr){
    /* 利用页目录自映射机制,通过虚拟地址访问页目录项(PDE)
     * 原理: 访问 0xFFFFF000+ 时,CPU两次查找PDE[1023],最终访问到页目录自己
     * 0xfffff000: PDE[1023]→页目录,再次查PDE[1023]→页目录,偏移0
     * PDE_IDX(vaddr) * 4: 页目录内的PDE偏移(PDE索引×4字节)
     */ 
    uint32_t* pde = (uint32_t*)((0xfffff000) + PDE_IDX(vaddr) * 4);
    return pde;
}





/* PDE/PTE 32位结构说明(从低位到高位):
 * ┌───────────────────────────────┬─────┬─┬─┬─┬─┬─┬─┬─┬─┬─┬─┐
 * │   页框物理地址高20位(31-12)    │AVL  │G│S│0│A│D│W│U│R│P│
 * └───────────────────────────────┴─────┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┘
 *   位31-12: 页框物理地址高20位(低12位隐含为0,因为4KB对齐)
 *   位11-9:  AVL - 可用位,OS自定义使用
 *   位8:     G - Global,全局页(TLB相关)
 *   位7:     S - Page Size (PDE中:0=4KB页, 1=4MB页)
 *   位6:     D - Dirty,脏位(页被写入时CPU自动置1)
 *   位5:     A - Accessed,访问位(页被访问时CPU自动置1)
 *   位4:     PCD - Page-level Cache Disable
 *   位3:     PWT - Page-level Write-Through
 *   位2:     U/S - User/Supervisor (0=内核态, 1=用户态可访问)
 *   位1:     R/W - Read/Write (0=只读, 1=可读写)
 *   位0:     P - Present (0=不存在会触发缺页, 1=存在)
 */


// 页表中添加虚拟地址_vaddr 与物理地址_page_phyaddr 的映射 
static void page_table_add(void* _vaddr, void* _page_phyaddr){
    uint32_t vaddr = (uint32_t) _vaddr, page_phyaddr =(uint32_t) _page_phyaddr;
    uint32_t* pde_p = pde_ptr(vaddr);  // 获取vaddr对应的页目录项指针
    uint32_t* pte_p = pte_ptr(vaddr);  // 获取vaddr对应的页表项指针

/*-----------------------注意-----------------------*/
    // 执行*pte_p会通过*pde_p查找页表,若pde_p的P位为0(页表不存在),
    // 会触发Page Fault。因此必须先检查*pde_p是否存在,不存在则需先创建页表
    
    /* 检查页目录项的P位(位0),判断对应的页表是否已存在 */
    if (*pde_p & 0x00000001){  // *pde_p & 0x1 检查P位,为1表示页表存在
        /* 页表已存在，直接在页表中添加页框映射 */
        ASSERT(!(*pte_p & 0x00000001));   // 断言PTE的P位为0(此页未被映射)
        
        /* 设置页表项:
         * page_phyaddr: 物理页框地址(高20位,低12位为0)
         * PG_US_U (4):  位2=1, 用户态可访问
         * PG_RW_W (2):  位1=1, 可读写
         * PG_P_1  (1):  位0=1, 页面存在
         * 结果: 物理地址 | 0x007 = [地址][...][U][W][P]
         */
        *pte_p = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);
    }else{
        // 页目录项不存在，所以要先创建页目录再创建页表项
        /* 页表中用到的页框一律从内核空间分配
         * 注意: 必须对 kernel_pool 加锁，因为调用者可能只持有 user_pool 的锁
         * （如用户进程 sys_malloc），而 palloc(&kernel_pool) 与内核线程的
         *  palloc 存在并发竞争。锁是可重入的，所以已持有 kernel_pool.lock
         *  的调用路径也不会死锁。 */
        lock_acquire(&kernel_pool.lock);
        uint32_t pde_phyaddr = (uint32_t) palloc(&kernel_pool);         //物理地址
        lock_release(&kernel_pool.lock);
        *pde_p = (pde_phyaddr | PG_US_U | PG_RW_W | PG_P_1);

        /* 如果是内核空间的虚拟地址（PDE[768..1022]），需要把新建的 PDE
         * 同步到所有已存在的用户进程页目录中。
         * 因为用户进程的页目录只在创建时复制过内核 PDE，后续内核新建的
         * PDE 不会自动出现在用户进程页目录中。用户进程在内核态（syscall）
         * 中如果访问这些新映射的内核地址，会因为 PDE 不存在而出错。 */
        uint32_t pde_idx = PDE_IDX(vaddr);
        if (pde_idx >= 768 && pde_idx < 1023) {
            uint32_t pde_val = *pde_p;
            kernel_pde_cache[pde_idx - 768] = pde_val;

            /* 写入内核页目录（通过 0xc0100000 固定映射访问） */
            KERNEL_PAGE_DIR[pde_idx] = pde_val;

            /* 遍历所有任务，把新 PDE 同步到每个用户进程的页目录 */
            extern struct list thread_all_list;
            struct list_elem* elem = thread_all_list.head.next;
            while (elem != &thread_all_list.tail) {
                struct task_struct* t =
                    elem2entry(struct task_struct, all_list_tag, elem);
                if (t->pgdir != NULL) {
                    t->pgdir[pde_idx] = pde_val;
                }
                elem = elem->next;
            }
        }

        /* 分配到的物理页地址 pde_phyaddr 对应的物理内存清 0，避免里面的陈旧数据变成了页表项，从而让页表混乱*/
        /* 访问到 pde 对应的物理地址，用 pte 取高 20 位便可,因为 pte 基于该 pde 对应的物理地址内再寻址
         * 把低 12 位置 0 便是该 pde 对应的物理页的起始*/
        memset((void*)((uint32_t)pte_p & 0xfffff000), 0, PG_SIZE);      //清0新页框
        ASSERT(!(*pte_p & 0x00000001));   // 断言PTE的P位为0(此页未被映射)
        *pte_p = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1);           // 设置页表项(映射)
    }
}


/* 分配 pg_cnt 个页空间，成功则返回起始虚拟地址，失败时返回 NULL */
void* malloc_page(enum pool_flags pf, uint32_t pg_cnt){
    ASSERT(pg_cnt > 0 && pg_cnt < 3840);
    /*********** malloc_page 的原理是三个动作的合成:    ***********
    * 1 通过 vaddr_get 在虚拟内存池中申请虚拟地址
    * 2 通过 palloc 在物理内存池中申请物理页
    * 3 通过 page_table_add 将以上得到的虚拟地址和物理地址在页表中完成映射
    * *********************************************************/

    void* vaddr_start = vaddr_get(pf, pg_cnt);
    if (vaddr_start == NULL)    return NULL;            // 申请虚拟地址失败
    uint32_t vaddr = (uint32_t)vaddr_start, cnt = pg_cnt;  // 显式转换指针为整数
    struct pool* mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;

    /* 因为虚拟地址是连续的，但物理地址可以是不连续的，所以逐个做映射*/
    while(cnt-- > 0){
        void* page_phyaddr = palloc(mem_pool);
        if (page_phyaddr == NULL){
            return NULL;    // 申请物理页失败
        }
        page_table_add((void*)vaddr, page_phyaddr);
        vaddr += PG_SIZE;
    }
    return vaddr_start;
}


/* 从内核物理内存池中申请 pg_cnt 页内存，成功则返回其虚拟地址，失败则返回 NULL */
void* get_kernel_pages(uint32_t pg_cnt){
    lock_acquire(&kernel_pool.lock);
    void* vaddr = malloc_page(PF_KERNEL, pg_cnt);           //得到的是虚拟地址
    if (vaddr != NULL) {
        memset(vaddr, 0, pg_cnt * PG_SIZE);                 //清空数据页(通过虚拟地址->实际地址)
    }
    lock_release(&kernel_pool.lock);
    return vaddr;
}

/* 在用户空间中申请 pg_cnt 页内存，并返回其虚拟地址 */
void* get_user_pages(uint32_t pg_cnt) {
    lock_acquire(&user_pool.lock);
    void* vaddr = malloc_page(PF_USER, pg_cnt);
    if (vaddr != NULL) {
        memset(vaddr, 0, pg_cnt * PG_SIZE);
    }
    lock_release(&user_pool.lock);
    return vaddr;
}

/* 将地址 vaddr 与 pf 池中的物理地址关联，仅支持一页空间分配 */
void* get_a_page(enum pool_flags pf, uint32_t vaddr) {
    struct pool* mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;
    lock_acquire(&mem_pool->lock);

    /* 先将虚拟地址对应的位图置 1 */
    struct task_struct* cur = running_thread();
    int32_t bit_idx = -1;

    /* 若当前是用户进程申请用户内存，就修改用户进程自己的虚拟地址位图 */
    if (cur->pgdir != NULL && pf == PF_USER) {
        bit_idx = (vaddr - cur->userprog_vaddr.vaddr_start) / PG_SIZE;
        ASSERT(bit_idx >= 0);
        /* 检查该虚拟地址是否已被分配 */
        if (bitmap_scan_test(&cur->userprog_vaddr.vaddr_bitmap, bit_idx)) {
            lock_release(&mem_pool->lock);
            PANIC("get_a_page: vaddr already allocated in user space!");
        }
        bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_idx, 1);

    } else if (cur->pgdir == NULL && pf == PF_KERNEL) {
        /* 如果是内核线程申请内核内存，就修改 kernel_vaddr */
        bit_idx = (vaddr - kernel_vaddr.vaddr_start) / PG_SIZE;
        ASSERT(bit_idx >= 0);
        /* 检查该虚拟地址是否已被分配 */
        if (bitmap_scan_test(&kernel_vaddr.vaddr_bitmap, bit_idx)) {
            lock_release(&mem_pool->lock);
            PANIC("get_a_page: vaddr already allocated in kernel space!");
        }
        bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx, 1);
    } else {
        PANIC("get_a_page: not allow kernel alloc userspace or user alloc kernelspace by get_a_page");
    }

    void* page_phyaddr = palloc(mem_pool);
    if (page_phyaddr == NULL) {
        lock_release(&mem_pool->lock);
        return NULL;
    }
    page_table_add((void*)vaddr, page_phyaddr);
    lock_release(&mem_pool->lock);
    return (void*)vaddr;
}

/* 得到虚拟地址映射到的物理地址 */
uint32_t addr_v2p(uint32_t vaddr) {
    uint32_t* pte = pte_ptr(vaddr);
    /* (*pte)的值是页表所在的物理页框地址，
     * 去掉其低 12 位的页表项属性+虚拟地址 vaddr 的低 12 位 */
    return ((*pte & 0xfffff000) + (vaddr & 0x00000fff));
}


// 在 m_pool 指向的物理内存池中分配 1 个物理页，成功则返回页框的物理地址，失败则返回 NULL
static void* palloc(struct pool* m_pool){
    int bit_idx = bitmap_scan(&m_pool->pool_bitmap, 1);  // 扫描空闲位
    if(bit_idx == -1){
        return NULL;
    }
    bitmap_set(&m_pool->pool_bitmap, bit_idx, 1);        // 将该位占用
    uint32_t page_phyaddr = m_pool->phy_addr_start + bit_idx * PG_SIZE;
    return (void*)page_phyaddr;
}

/* 页表中添加虚拟地址_vaddr 与物理地址_page_phyaddr 的映射 */


/* 初始化内存池 */
static void mem_pool_init(uint32_t all_mem){
    put_str("mem_pool_init start\n");
    uint32_t page_table_size = PG_SIZE * 256;
    // 先只加载内核部分
    // 页表大小 = 1 页的页目录表 + 第 0 和第 768 个页目录项指向同一个页表 +第 769～1022 个页目录项共指向 254 个页表，共 256 个页框
    
    uint32_t used_mem = page_table_size + 0x100000;  // 0x100000 为低端 1MB 内存
    uint32_t free_mem = all_mem - used_mem;
    uint16_t all_free_pages = free_mem / PG_SIZE;           // 计算空闲的页框数

    uint16_t kernel_free_pages = all_free_pages / 2;        // 内核内存池和用户内存池平分剩余内存
    uint16_t user_free_pages = all_free_pages - kernel_free_pages;


    /* 为简化位图操作，余数不处理，坏处是这样做会丢内存。 好处是不用做内存的越界检查，因为位图表示的内存少于实际物理内存*/
    uint32_t kbm_length = kernel_free_pages / 8;           // 内核内存池位图长度，单位字节
    // Kernel BitMap 的长度，位图中的一位表示一页，以字节为单位
    uint32_t ubm_length = user_free_pages / 8;             // 用户内存池位图长度，单位字节

    uint32_t kp_start = used_mem;                          // 内核内存池起始地址
    uint32_t up_start = kp_start + kernel_free_pages * PG_SIZE; // 用户内存池起始地址

    kernel_pool.phy_addr_start = kp_start;
    user_pool.phy_addr_start = up_start;

    kernel_pool.pool_size = kernel_free_pages * PG_SIZE;
    user_pool.pool_size = user_free_pages * PG_SIZE;

    kernel_pool.pool_bitmap.btmp_bytes_len = kbm_length;
    user_pool.pool_bitmap.btmp_bytes_len = ubm_length;


    /*********  内核内存池和用户内存池位图  ***********/
    // 内核使用的最高地址是 0xc009f000，这是主线程的栈地址
    kernel_pool.pool_bitmap.bits = (uint8_t*)MEM_BITMAP_BASE;
    /* 用户内存池的位图紧跟在内核内存池位图之后 */
    user_pool.pool_bitmap.bits = (uint8_t*)(MEM_BITMAP_BASE + kbm_length);


/************************************************************/
/**************************输出内存池信息**********************/
/************************************************************/

    put_str("kernel_pool_bitmap_start: ");
    put_hex((int)kernel_pool.pool_bitmap.bits);
    put_str("\nuser_pool_bitmap_start: ");
    put_hex((int)user_pool.pool_bitmap.bits);

    put_str("\n");
    put_str("kernel_pool_phy_addr_start: ");
    put_hex(kernel_pool.phy_addr_start);
    put_str("\nuser_pool_phy_addr_start: ");
    put_hex(user_pool.phy_addr_start);
    put_char('\n');

    /* 将位图置 0 */
    bitmap_init(&kernel_pool.pool_bitmap);
    bitmap_init(&user_pool.pool_bitmap);

    /* 初始化内存池的锁 */
    lock_init(&kernel_pool.lock);
    lock_init(&user_pool.lock);

    /* 下面初始化内核虚拟地址的位图，按实际物理内存大小生成数组。*/
    kernel_vaddr.vaddr_bitmap.btmp_bytes_len = kbm_length;
    // 用于维护内核堆的虚拟地址，所以要和内核内存池大小一致
    kernel_vaddr.vaddr_bitmap.bits = (uint8_t*)(MEM_BITMAP_BASE + kbm_length + ubm_length); //地址在内核内存池和用户内存池之后
    kernel_vaddr.vaddr_start = K_HEAP_START;            // 内核堆起始地址
    bitmap_init(&kernel_vaddr.vaddr_bitmap);

    /* 保留虚拟地址 0xc0100000（位图 bit 0），用于固定映射到内核页目录
     * 物理地址 0x100000。这样 kernel_page_dir 就能在任何 CR3 下通过
     * 内核空间 PDE（已同步到所有进程页目录）访问内核页目录。 */
    bitmap_set(&kernel_vaddr.vaddr_bitmap, 0, 1);       // 标记已占用
    page_table_add((void*)K_HEAP_START, (void*)0x100000); // 建立映射

    put_str("mem_pool_init done\n");

};

//--------------------------------------------------//
/* 内存管理部分初始化入口 */
//-------------------------------------------------//
void mem_init(){
    put_str("mem_init start\n");
    /* total_mem_bytes 地址 = LOADER_BASE_ADDR(0x900) + 8(jmp+nops) + 4*8(GDT) + 60*8(reserved) = 0xB08 */
    uint32_t mem_bytes_total = (*(uint32_t*)(0xb08));
    mem_pool_init(mem_bytes_total);
    block_desc_init(k_block_descs);

    /* 初始化内核 PDE 缓存：此时 CR3 指向内核页目录，
     * 通过自映射 0xfffff000 可以直接读取 PDE[768..1022] */
    uint32_t i;
    for (i = 0; i < 255; i++) {
        kernel_pde_cache[i] = *(uint32_t*)(0xfffff000 + (768 + i) * 4);
    }

    put_str("mem_init done\n");
}

/* 内核内存块规格描述符数组 */
struct mem_block_desc k_block_descs[MEM_BLOCK_DESC_CNT];

/* 初始化 mem_block_desc 数组，7 种规格：16/32/64/128/256/512/1024 */
void block_desc_init(struct mem_block_desc* desc_array) {
    uint32_t block_size = 16;
    uint32_t i;
    for (i = 0; i < MEM_BLOCK_DESC_CNT; i++) {
        desc_array[i].block_size = block_size;
        desc_array[i].blocks_per_arena =
            (PG_SIZE - sizeof(struct arena)) / block_size;
        list_init(&desc_array[i].free_list);
        block_size <<= 1;   // 16 → 32 → 64 → ... → 1024
    }
}

/* 释放 pg_cnt 个连续虚拟页（同时释放物理页和虚拟地址位图）*/
void mfree_page(enum pool_flags pf, void* _vaddr, uint32_t pg_cnt) {
    uint32_t vaddr = (uint32_t)_vaddr;
    uint32_t cnt = 0;
    ASSERT(pg_cnt >= 1 && vaddr % PG_SIZE == 0);

    struct pool* mem_pool = (pf == PF_KERNEL) ? &kernel_pool : &user_pool;

    while (cnt < pg_cnt) {
        /* 1. 找到虚拟地址对应的物理地址，清除物理页位图 */
        uint32_t paddr = addr_v2p(vaddr + cnt * PG_SIZE);
        ASSERT((paddr % PG_SIZE) == 0 &&
               paddr >= mem_pool->phy_addr_start &&
               paddr < mem_pool->phy_addr_start + mem_pool->pool_size);

        uint32_t bit_idx = (paddr - mem_pool->phy_addr_start) / PG_SIZE;
        bitmap_set(&mem_pool->pool_bitmap, bit_idx, 0);         // 将该位置 0

        /* 2. 清除页表项（PTE 置 0）*/
        uint32_t* pte = pte_ptr(vaddr + cnt * PG_SIZE);
        *pte = 0;
        /* 刷新 TLB 中该虚拟地址的缓存 */
        asm volatile ("invlpg (%0)" : : "r" (vaddr + cnt * PG_SIZE) : "memory");

        cnt++;
    }

    /* 3. 清除虚拟地址位图 */
    if (pf == PF_KERNEL) {
        uint32_t bit_idx = (vaddr - kernel_vaddr.vaddr_start) / PG_SIZE;
        uint32_t i;
        for (i = 0; i < pg_cnt; i++) {
            bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx + i, 0);
        }
    } else {
        struct task_struct* cur = running_thread();
        uint32_t bit_idx =
            (vaddr - cur->userprog_vaddr.vaddr_start) / PG_SIZE;
        uint32_t i;
        for (i = 0; i < pg_cnt; i++) {
            bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_idx + i, 0);
        }
    }
}

/* 获取/释放内存池的锁，供 syscall.c 使用 */
void mem_lock_acquire(enum pool_flags pf) {
    struct pool* p = (pf == PF_KERNEL) ? &kernel_pool : &user_pool;
    lock_acquire(&p->lock);
}

void mem_lock_release(enum pool_flags pf) {
    struct pool* p = (pf == PF_KERNEL) ? &kernel_pool : &user_pool;
    lock_release(&p->lock);
}