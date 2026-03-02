#include "process.h"
#include "thread.h"
#include "global.h"
#include "memory.h"
#include "tss.h"
#include "interrupt.h"
#include "debug.h"
#include "string.h"
#include "bitmap.h"
#include "list.h"
#include "print.h"

#define PG_SIZE 4096

extern struct list thread_all_list;
extern void enqueue_task(struct task_struct* pthread);

/* 用户栈的虚拟地址（位于用户空间顶部，3GB - 4KB） */
#define USER_STACK3_VADDR  (0xc0000000 - PG_SIZE)

extern void intr_exit(void);

/* 构建用户进程初始上下文信息 */
void start_process(void* filename_) {
    void* function = filename_;
    struct task_struct* cur = running_thread();
    
    /* self_kstack 当前指向 thread_stack 的起始位置，
     * 加上 sizeof(struct thread_stack) 后跳过它，
     * 此时指向 intr_stack 的起始位置。
     * 注意：self_kstack 是 uint32_t* 类型，指针算术会自动 ×4，
     * 所以必须先转为 char* 再做字节级偏移 */
    cur->self_kstack = (uint32_t*)((char*)cur->self_kstack + sizeof(struct thread_stack));
    struct intr_stack* proc_stack = (struct intr_stack*)cur->self_kstack;
    
    /* 初始化通用寄存器为 0 */
    proc_stack->edi = proc_stack->esi = 0;
    proc_stack->ebp = proc_stack->esp_dummy = 0;
    proc_stack->ebx = proc_stack->edx = 0;
    proc_stack->ecx = proc_stack->eax = 0;
                                                                                        
    /* 初始化段寄存器 */
    proc_stack->gs = 0;  // 用户态用不上，直接初始为 0
    proc_stack->ds = proc_stack->es = proc_stack->fs = SELECTOR_U_DATA;
    
    /* 设置用户程序入口地址 */
    proc_stack->eip = function;
    proc_stack->cs = SELECTOR_U_CODE;
    
    /* 设置 eflags: 开中断，IOPL=0（用户态不能直接访问IO端口） */
    proc_stack->eflags = (EFLAGS_IOPL_0 | EFLAGS_MBS | EFLAGS_IF_1);
    
    /* 分配用户3级栈，并设置 esp 指向栈顶（高地址） */
    proc_stack->esp = (void*)((uint32_t)get_a_page(PF_USER, USER_STACK3_VADDR) + PG_SIZE);
    proc_stack->ss = SELECTOR_U_DATA;
    
    /* 切换到用户态：
     * 1. 将 esp 指向 proc_stack（模拟中断返回前的栈状态）
     * 2. 跳转到 intr_exit，通过 iret 指令返回用户态 */
    asm volatile ("movl %0, %%esp; jmp intr_exit" 
                  : 
                  : "g" (proc_stack) 
                  : "memory");
}

/* 激活页表 */
void page_dir_activate(struct task_struct* p_thread) {
    /* 默认为内核的页目录物理地址 */
    uint32_t pagedir_phy_addr = 0x100000;
    
    if (p_thread->pgdir != NULL) {
        pagedir_phy_addr = addr_v2p((uint32_t)p_thread->pgdir);
    }
    
    asm volatile ("movl %0, %%cr3" : : "r" (pagedir_phy_addr) : "memory");
}

/* 激活线程或进程的页表，更新 tss 中的 esp0 为进程的特权级 0 的栈 */ 
void process_activate(struct task_struct* p_thread) {
    ASSERT(p_thread != NULL);
    
    /* 切换页表 */
    page_dir_activate(p_thread);
    
    /* 内核线程特权级本身就是 0，处理器进入中断时并不会
     * 从 tss 中获取 0 特权级栈地址，故不需要更新 esp0 */
    
    // 如果是用户进程
    if (p_thread->pgdir) {
        /* 更新该进程的 esp0，用于此进程被中断时保留上下文 */
        update_tss_esp(p_thread);
    }
}

/* 创建页目录表，将当前页表的内核空间部分（高1GB）复制到新页目录表，
 * 成功则返回页目录的虚拟地址，否则返回 NULL */
uint32_t* create_page_dir(void) {
    /* 用户进程的页目录表不能让用户直接访问到，所以在内核空间中申请 */
    uint32_t* page_dir_vaddr = get_kernel_pages(1);         // 页目录地址
    if (page_dir_vaddr == NULL) {
        put_str("create_page_dir: get_kernel_pages failed!\n");
        return NULL;
    }
    
    /************************** 1. 复制内核空间的页目录项 **************************
     * page_dir_vaddr + 0x300*4 是内核页目录的第 768 项（0x300 = 768）
     * 0xfffff000 是当前页目录表的虚拟地址（通过页目录自映射访问）
     * 0xfffff000 + 0x300*4 表示当前页目录表中从第 768 项开始的地址
     * 
     * 因为内核空间是所有进程共享的，所以需要将内核的页目录项（768-1023）
     * 复制到新创建的页目录表中
     ******************************************************************************/
    memcpy((uint32_t*)((uint32_t)page_dir_vaddr + 0x300*4), 
           (uint32_t*)(0xfffff000 + 0x300*4), 
           1024);
    
    /* 将复制过来的内核PDE的U/S位设为用户可访问
     * 这样用户进程在Ring3也能访问内核空间的代码和数据
     * 注意：这仅用于当前测试阶段（用户进程代码实际在内核空间）
     * 真正的用户程序应该通过ELF加载到用户空间 */
    uint32_t pde_idx;
    for (pde_idx = 768; pde_idx < 1023; pde_idx++) {
        if (page_dir_vaddr[pde_idx] & PG_P_1) {
            page_dir_vaddr[pde_idx] |= PG_US_U;
        }
    }
    
    /************************** 2. 更新页目录地址 **************************
     * 将新页目录表的最后一项（1023 项）设置为自己的物理地址，
     * 实现页目录表的自映射
     ************************************************************************/
    uint32_t new_page_dir_phy_addr = addr_v2p((uint32_t)page_dir_vaddr);
    page_dir_vaddr[1023] = new_page_dir_phy_addr | PG_US_U | PG_RW_W | PG_P_1;
    
    return page_dir_vaddr;
}

/* 创建用户进程虚拟地址位图 */
void create_user_vaddr_bitmap(struct task_struct* user_prog) {
    /* 用户进程虚拟地址范围：USER_VADDR_START (0x08048000) ~ 0xc0000000 */
    user_prog->userprog_vaddr.vaddr_start = USER_VADDR_START; 
    
    /* 计算位图需要的页数和字节数
     * (0xc0000000 - 0x08048000) / PG_SIZE / 8 
     * 分母中除以 PG_SIZE 是转换成页数，除以 8 是转换成字节数（因为位图每一位代表一页）
     */
    uint32_t bitmap_pg_cnt = DIV_ROUND_UP(
        (0xc0000000 - USER_VADDR_START) / PG_SIZE / 8,
        PG_SIZE
    );
    
    /* 为位图分配内核空间 */
    user_prog->userprog_vaddr.vaddr_bitmap.bits = get_kernel_pages(bitmap_pg_cnt);
    if (user_prog->userprog_vaddr.vaddr_bitmap.bits == NULL) {
        put_str("create_user_vaddr_bitmap: get_kernel_pages for bitmap failed!\n");
        return;
    }
    
    /* 初始化位图的长度 */
    user_prog->userprog_vaddr.vaddr_bitmap.btmp_bytes_len = 
        (0xc0000000 - USER_VADDR_START) / PG_SIZE / 8;
    
    /* 初始化位图，全部置0 */
    bitmap_init(&user_prog->userprog_vaddr.vaddr_bitmap);
}

/* 创建用户进程 */
void process_execute(void* filename, char* name) {
    /* pcb 内核的数据结构，安置在内核空间 */
    struct task_struct* thread = get_kernel_pages(1);
    if (thread == NULL) {
        put_str("process_execute: get_kernel_pages failed!\n");
        return;
    }
    
    /* 初始化进程，默认优先级为31 */
    init_thread(thread, name, 31);
    create_user_vaddr_bitmap(thread);
    block_desc_init(thread->u_block_descs);  // 初始化用户进程自己的内存块描述符
    thread_create(thread, start_process, filename);
    
    /* 创建页目录表 */                                 
    thread->pgdir = create_page_dir();
    
    /* 将线程加入 CFS 就绪队列 */
    enqueue_task(thread);
    
    /* 加入全局线程列表 */
    enum intr_status old_status = intr_disable();
    ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
    list_append(&thread_all_list, &thread->all_list_tag);
    intr_set_status(old_status);
}
