#include "thread.h"
#include "stdint.h"
#include "string.h"
#include "global.h"
#include "interrupt.h"
#include "memory.h"
#include "sched.h"
#include "list.h"
#include "print.h"
#include "debug.h"
#include "sync.h"

#define PG_SIZE 4096

static uint32_t next_pid = 0;      // 全局 PID 计数器
static struct lock pid_lock;       // 保护 PID 分配的锁

/* 分配 PID（线程安全）*/
static uint32_t allocate_pid(void) {
    lock_acquire(&pid_lock);
    uint32_t pid = next_pid++;
    lock_release(&pid_lock);
    return pid;
}

struct task_struct* main_thread;    // 主线程 PCB
struct task_struct* idle_thread;    // idle 线程
struct list thread_ready_list;      // 就绪队列（已被 CFS 红黑树替代，保留用于兼容）
struct list thread_all_list;        // 所有线程队列

/* 获取当前线程 PCB 指针 */
struct task_struct* running_thread(void) {
    uint32_t esp;
    asm ("mov %%esp, %0" : "=g" (esp));
    /* 取 PCB 起始地址 */
    return (struct task_struct*)(esp & 0xfffff000);
}

/* 将 main 函数初始化为主线程 */
static void make_main_thread(void) {
    /* main 函数运行时，loader 已经将栈指针 esp 设置在 0xc009f000
     * main 函数的 PCB 在页的最低地址，即 0xc009e000 */
    main_thread = running_thread();
    init_thread(main_thread, "main", 0);  // 优先级设为 0 (nice=0, weight=1024)
    
    /* main 线程已经在运行，需要设置为 RUNNING 状态 */
    main_thread->status = TASK_RUNNING;
    
    /* main 函数是当前线程，不在就绪队列中
     * 所以只将其加入全局线程列表 */
    list_append(&thread_all_list, &main_thread->all_list_tag);
    
    /* 设置为当前运行线程 */
    extern struct task_struct* current_thread;  // 声明在 sched.c 中
    current_thread = main_thread;
    
    /* 主线程已经在运行，设置 exec_start */
    main_thread->exec_start = main_thread->elapsed_ticks;
}

/* 系统空闲时运行的 idle 线程 */
static void idle(void* arg UNUSED) {
    while (1) {
        thread_block(TASK_BLOCKED);
        /* 执行 hlt 时必须要保证目前处在开中断的情况下 */
        asm volatile ("sti; hlt" : : : "memory");
    }
}

/* 初始化线程环境 */
void thread_init(void) {
    put_str("thread_init start\n");
    list_init(&thread_all_list);
    
    /* 初始化 PID 分配锁 */
    lock_init(&pid_lock);
    
    sched_init();  // 初始化 CFS 调度器
    
    /* 将当前 main 函数创建为主线程 */
    make_main_thread();
    
    /* 创建 idle 线程，当就緒队列为空时运行 */
    idle_thread = thread_start("idle", 10, idle, NULL);
    
    put_str("thread_init done\n");
}


/* 由 kernel_thread 去执行 function(func_arg) */
static void kernel_thread(thread_func* function, void* func_arg) {
    /* 执行 function 前要开中断，避免后面的时钟中断被屏蔽，而无法调度其他线程 */
    asm volatile ("sti");  // 开中断，避免时钟中断被屏蔽
    function(func_arg);
    
    /* 如果线程函数返回(理论上不应该),进入死循环避免返回到未知地址 */
    while(1);
}

//初始化线程栈 thread_stack，将待执行的函数和参数放到 thread_stack 中相应的位置
void thread_create(struct task_struct* pthread, thread_func function, void* func_arg){
    // 先预留中断使用栈的空间，thread.h 中定义的结构 intr_stack
    // 注意：self_kstack 是 uint32_t*，指针算术会自动 ×4，必须用字节级运算
    pthread->self_kstack = (uint32_t*)((char*)pthread->self_kstack - sizeof(struct intr_stack));
    
    // 初始化中断栈 (用于第一次调度时的环境)
    struct intr_stack* intr_0_stack = (struct intr_stack*)pthread->self_kstack;
    intr_0_stack->vec_no = 0;
    intr_0_stack->edi = intr_0_stack->esi = intr_0_stack->ebp = 0;
    intr_0_stack->esp_dummy = 0;
    intr_0_stack->ebx = intr_0_stack->edx = intr_0_stack->ecx = intr_0_stack->eax = 0;
    intr_0_stack->gs = 0;  // 内核线程不使用 gs,由 put_char 等函数自己设置
    intr_0_stack->ds = intr_0_stack->es = intr_0_stack->fs = SELECTOR_K_DATA;  // 内核数据段
    intr_0_stack->err_code = 0;  // 错误码
    intr_0_stack->eip = 0;  // 这个会被 thread_stack 的 eip 覆盖
    intr_0_stack->cs = SELECTOR_K_CODE;  // 内核代码段
    intr_0_stack->eflags = (EFLAGS_IOPL_0 | EFLAGS_MBS | EFLAGS_IF_1);  // IF=1 开中断
    intr_0_stack->esp = 0;
    intr_0_stack->ss = 0;

    // 再留出线程栈空间，thread_stack
    pthread->self_kstack = (uint32_t*)((char*)pthread->self_kstack - sizeof(struct thread_stack));
    
    struct thread_stack* kthread_stack = (struct thread_stack*)pthread->self_kstack;
    kthread_stack->eip = kernel_thread;
    kthread_stack->function = function;
    kthread_stack->func_arg = func_arg;
    kthread_stack->ebp = kthread_stack->ebx = kthread_stack->esi = kthread_stack->edi = 0;
}

/* 初始化线程基本信息 */
void init_thread(struct task_struct* pthread, char* name, int prio){
    memset(pthread, 0, sizeof(*pthread));
    
    pthread->pid = allocate_pid();  // 分配 PID
    
    strcpy(pthread->name, name);
    pthread->status = TASK_READY;  // 新创建的线程应该是就绪状态
    pthread->priority = prio;
    
    /* 初始化 CFS 调度相关字段 */
    pthread->vruntime = 0;
    pthread->weight = nice_to_weight(prio - 20);  // priority 映射到 nice 值
    pthread->exec_start = 0;
    pthread->sum_exec_runtime = 0;
    pthread->rb_node.rb_parent = NULL;
    pthread->rb_node.rb_left = NULL;
    pthread->rb_node.rb_right = NULL;
    pthread->rb_node.rb_color = RB_RED;
    
    /* self_kstack 是线程自己在内核态下使用的栈顶地址 */
    // PCB 位于页的起始处（低地址），栈从页末尾（高地址）向下 (低地址) 增长
    pthread->self_kstack = (uint32_t*)((uint32_t)pthread + PG_SIZE);  
    
    /* 初始化文件描述符表: 0=stdin, 1=stdout, 2=stderr 填占位, 其余置 -1 */
    pthread->fd_table[0] = 0;   // stdin
    pthread->fd_table[1] = 1;   // stdout
    pthread->fd_table[2] = 2;   // stderr
    uint8_t fd_idx = 3;
    while (fd_idx < MAX_FILES_OPEN_PER_PROC) {
        pthread->fd_table[fd_idx] = -1;
        fd_idx++;
    }
    pthread->cwd_inode_nr = 0;  // 默认工作目录为根目录
    
    pthread->stack_magic = 0x19870916;              // 自定义的魔数
}

/* 创建一优先级为 prio 的线程，线程名为 name，线程所执行的函数是 function(func_arg) */
struct task_struct* thread_start(char* name, int prio, thread_func function, void* func_arg){
    // pcb 都位于内核空间，包括用户进程的 pcb
    struct task_struct* thread = get_kernel_pages(1);   // 分配一页内核空间作为线程的 pcb,
                                                        // get_kernel_pages 会返回虚拟地址
    
    init_thread(thread, name, prio);
    thread_create(thread, function, func_arg);

    /* 将线程加入 CFS 就绪队列 */
    enqueue_task(thread);
    
    /* 加入全局线程列表 */
    list_append(&thread_all_list, &thread->all_list_tag);

    return thread;
}

/* 当前线程将自己阻塞，标志其状态为 stat. */
void thread_block(enum task_status stat){
    ASSERT((stat == TASK_BLOCKED) || (stat == TASK_WAITING) || (stat == TASK_HANGING));
    
    enum intr_status old_status = intr_disable();
    struct task_struct* cur_thread = running_thread();
    cur_thread->status = stat;
    schedule();  // 调度其他线程运行
    intr_set_status(old_status);
}

/* 将线程 pthread 解除阻塞 */
void thread_unblock(struct task_struct* pthread){
    enum intr_status old_status = intr_disable();
    ASSERT(pthread->status == TASK_BLOCKED ||
           pthread->status == TASK_WAITING ||
           pthread->status == TASK_HANGING);
    pthread->status = TASK_READY;
    /* 加入就绪队列 */
    enqueue_task(pthread);
    intr_set_status(old_status);
}
/* 主动让出 cpu，换其他线程运行 */
void thread_yield(void) {
    struct task_struct* cur = running_thread();
    enum intr_status old_status = intr_disable();
    /* 将当前线程重新加入就緒队列 */
    cur->status = TASK_READY;
    enqueue_task(cur);
    schedule();  // 调度器会把当前线程从队列取出再运行
    intr_set_status(old_status);
}