#include "sync.h"
#include "interrupt.h"
#include "debug.h"
#include "thread.h"
#include "list.h"

/* 初始化信号量 sema */
void sema_init(struct semaphore* p_sema, uint8_t value) {
    p_sema->value = value;
    list_init(&p_sema->waiters);  // 初始化信号量的等待队列
}

/* 初始化锁 plock */
void lock_init(struct lock* plock){
    plock->holder = NULL;
    plock->holder_repeat_nr = 0;
    sema_init(&plock->semaphore, 1);    // 初始值为 1
}

/* 信号量 down 操作 (P 操作) */
void sema_down(struct semaphore* p_sema) {
    enum intr_status old_status = intr_disable();
    
    /* 如果信号量值为 0,当前线程需要阻塞
     * 注意: while 循环的原因是防止虚假唤醒 (spurious wakeup)
     * 线程被唤醒后需要重新检查条件 */
    while(p_sema->value == 0) {
        /* 将当前线程加入等待队列 */
        list_append(&p_sema->waiters, &running_thread()->general_tag);
        /* 阻塞当前线程,让出 CPU */
        thread_block(TASK_BLOCKED);
        /* 被唤醒后会从这里继续执行,重新检查 while 条件 */
    }
    

    p_sema->value--;
    ASSERT(p_sema->value >= 0);
    
    intr_set_status(old_status);
}

/* 信号量 up 操作 (V 操作) */
void sema_up(struct semaphore* p_sema) {
    enum intr_status old_status = intr_disable();
    
    ASSERT(p_sema->value >= 0);
    
    /* 如果有线程在等待,唤醒第一个 */
    if(!list_empty(&p_sema->waiters)) {
        struct task_struct* thread_blocked = elem2entry(struct task_struct, 
                                                        general_tag, 
                                                        list_pop(&p_sema->waiters));
        thread_unblock(thread_blocked);
    }
    
    /* 释放信号量 */
    p_sema->value++;
    ASSERT(p_sema->value >= 1);
    
    intr_set_status(old_status);
}

/* 获取锁 plock */
void lock_acquire(struct lock* plock) {
    /* 如果当前线程已经持有该锁,递增重复申请次数即可 */
    if(plock->holder != running_thread()) {
        /* 不是当前线程持有,需要通过信号量获取 */
        sema_down(&plock->semaphore);
        plock->holder = running_thread();
        ASSERT(plock->holder_repeat_nr == 0);
        plock->holder_repeat_nr = 1;
    } else {
        /* 已经持有锁,递增计数 */
        plock->holder_repeat_nr++;
    }
}

/* 释放锁 plock */
void lock_release(struct lock* plock) {
    ASSERT(plock->holder == running_thread());
    
    /* 如果重复申请了多次,只递减计数 */
    if(plock->holder_repeat_nr > 1) {
        plock->holder_repeat_nr--;
        return;
    }
    
    /* 最后一次释放,清空持有者并释放信号量 */
    ASSERT(plock->holder_repeat_nr == 1);
    plock->holder = NULL;
    plock->holder_repeat_nr = 0;
    sema_up(&plock->semaphore);
}