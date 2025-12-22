#ifndef __THREAD_THREAD_H
#define __THREAD_THREAD_H
#include "stdint.h"
#include "list.h"

/* 自定义通用函数类型，它将在很多线程函数中作为形参类型 */
typedef void thread_func(void*);
//thread_func my_function 等价于 void my_function(void*);

/* 进程或线程的状态 */
enum task_status {
    TASK_RUNNING,        // 运行态
    TASK_READY,          // 就绪态
    TASK_BLOCKED,        // 阻塞态
    TASK_WAITING,        // 等待态
    TASK_HANGING,        // 挂起态
    TASK_DIED            // 死亡态
};

/***********   中断栈 intr_stack   ***********
 * 此结构用于中断发生时保护程序（线程或进程）的上下文环境：
 * 进程或线程被外部中断或软中断打断时，会按照此结构压入上下文
 * 寄存器，intr_exit 中的出栈操作是此结构的逆操作
 * 此栈在线程自己的内核栈中位置固定，所在页的最顶端
 * ************************************************/
 struct intr_stack {
    uint32_t vec_no;                // kernel.S 宏 VECTOR 中 push %1 压入的中断号
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_dummy;             // 虽然 pushad 会把 esp 压入，但 esp 是不断变化的
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;

    /* 以下由 cpu 从低特权级进入高特权级时压入 */
    uint32_t err_code;              // err_code 会被压入在 eip 之后
    void (*eip)(void);              // 中断发生时的 eip, pc(程序计数器)
    uint32_t cs;
    uint32_t eflags;                // 中断发生时的 eflags
    void* esp;                      // 中断发生时的 esp
    uint32_t ss;                    // 中断发生时的 ss
 };

 /***********   线程栈 thread_stack   ***********
  * 线程自己的栈，用于存储线程中待执行的函数
  * 此结构在线程自己的内核栈中位置不固定，由 thread_create
  * 在创建线程时分配位置
  * 仅用在 switch_to 时保存线程环境。
  * 实际栈中的布局（从高地址到低地址）：
  * +------------------+
  * |    func_arg      |  ← 参数
  * +------------------+
  * |    function      |  ← 函数指针
  * +------------------+
  * | unused_retaddr   |  ← 占位返回地址
  * +------------------+
  * |      eip         |  ← 第一次指向kernel_thread，后续指向switch_to返回地址
  * +------------------+
  * |      ebp         |  ← switch_to中保存
  * +------------------+
  * |      ebx         |  ← switch_to中保存
  * +------------------+
  * |      edi         |  ← switch_to中保存
  * +------------------+
  * |      esi         |  ← switch_to中保存 (栈顶，esp指向这里)
  * +------------------+
  * ***********************************************/
 
struct thread_stack {
    uint32_t ebp;
    uint32_t ebx;
    uint32_t edi;
    uint32_t esi;

    /* 线程第一次执行时，eip 指向待调用的函数 kernel_thread 
     * 其他时候，eip 是指向 switch_to 的返回地址 */
    void (*eip) (thread_func* func, void* func_arg);

    /*****  以下仅供第一次被调度上 cpu 时使用   *****/
    /* 参数 unused_ret 只为占位置为返回地址 */
    void (*unused_retaddr);          // 占位，作为kernel_thread的返回地址（实际不会用到）
    thread_func* function;           // 由 kernel_thread 所调用的函数名
    void* func_arg;                  // 由 kernel_thread 所调用的函数所需的参数
};

/* 进程或线程的 pcb，程序控制块 */
struct task_struct{
    uint32_t* self_kstack;          // 各内核线程都用自己的内核栈
    enum task_status status;        // 线程状态
    uint8_t priority;               // 线程优先级
    char name[16];
    uint8_t ticks;                  // 每次在处理器上执行的时间嘀嗒数

    /* 此任务自上 cpu 运行后至今占用了多少 cpu 嘀嗒数, 也就是此任务执行了多久 */
    uint32_t elapsed_ticks;
    struct list_elem general_tag;   // 用于线程在一般队列中的结点
    struct list_elem all_list_tag;  // 用于线程队列 thread_all_list 中
    
    uint32_t* pgdir;                 // 进程自己页表的虚拟地址  

    uint32_t stack_magic;           // 栈的边界标记，用于检测栈的溢出
};





/* 创建一优先级为 prio 的线程，线程名为 name，线程所执行的函数是 function(func_arg) */
struct task_struct* thread_start(char* name, int prio, thread_func function, void* func_arg);   


#endif