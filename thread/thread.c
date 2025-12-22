#include "thread.h"
#include "stdint.h"
#include "string.h"
#include "global.h"
#include "memory.h"

#define PG_SIZE 4096

struct task_struct* main_thread;    // 主线程 PCB 
struct list thread_ready_list;      // 就绪队列
struct list thread_all_list;        // 所有线程队列
static struct list_elem* thread_tag;// 用于保存队列中的线程结点


/* 由 kernel_thread 去执行 function(func_arg) */
static void kernel_thread(thread_func* function, void* func_arg) {
    /* 执行 function 前要开中断，避免后面的时钟中断被屏蔽，而无法调度其他线程 */
    //asm volatile ("sti");  // 开中断，避免时钟中断被屏蔽
    function(func_arg);
}

//初始化线程栈 thread_stack，将待执行的函数和参数放到 thread_stack 中相应的位置
void thread_create(struct task_struct* pthread, thread_func function, void* func_arg){
    // 先预留中断使用栈的空间，thread.h 中定义的结构 intr_stack
    pthread->self_kstack -= sizeof(struct intr_stack);

    // 再留出线程栈空间，thread_stack
    pthread->self_kstack -= sizeof(struct thread_stack);
    
    struct thread_stack* kthread_stack = (struct thread_stack*)pthread->self_kstack;
    kthread_stack->eip = kernel_thread;
    kthread_stack->function = function;
    kthread_stack->func_arg = func_arg;
    kthread_stack->ebp = kthread_stack->ebx = kthread_stack->esi = kthread_stack->edi = 0;
}

/* 初始化线程基本信息 */
void init_thread(struct task_struct* pthread, char* name, int prio){
    memset(pthread, 0, sizeof(*pthread));
    strcpy(pthread->name, name);
    pthread->status = TASK_RUNNING;
    pthread->priority = prio;
    
    /* self_kstack 是线程自己在内核态下使用的栈顶地址 */
    // PCB 位于页的起始处（低地址），栈从页末尾（高地址）向下 (低地址) 增长
    pthread->self_kstack = (uint32_t*)((uint32_t)pthread + PG_SIZE);  
    pthread->stack_magic = 0x19870916;              // 自定义的魔数
}

/* 创建一优先级为 prio 的线程，线程名为 name，线程所执行的函数是 function(func_arg) */
struct task_struct* thread_start(char* name, int prio, thread_func function, void* func_arg){
    // pcb 都位于内核空间，包括用户进程的 pcb
    struct task_struct* thread = get_kernel_pages(1);   // 分配一页内核空间作为线程的 pcb,
                                                        // get_kernel_pages 会返回虚拟地址
    init_thread(thread, name, prio);
    thread_create(thread, function, func_arg);

    asm volatile ("movl %0, %%esp; pop %%ebp; pop %%ebx; pop %%edi; pop %%esi; \
                   ret": : "g" (thread->self_kstack) : "memory"); // 从线程栈中恢复寄存器值，并跳转到 eip 指向的函数去运行
    // ret会把栈顶的数据作为返回地址送上处理器的 EIP 寄存器, 此处也就是 kernel_thread

    return thread;
}