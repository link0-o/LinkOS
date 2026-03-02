#ifndef __THREAD_SYNC_H
#define __THREAD_SYNC_H

#include "stdint.h"
#include "rbtree.h"
#include "list.h"
#include "thread.h"
 
/* 信号量结构 */
struct semaphore {
    uint8_t value;                  // 信号量值
    struct list waiters;            // 等待该信号量的线程队列
};

/* 锁结构 */
struct lock {
    struct task_struct* holder;     // 锁的持有者
    struct semaphore semaphore;     // 用于实现锁的信号量
    uint32_t holder_repeat_nr;      // 锁的持有者重复申请锁的次数
};

/* 函数声明 */
void sema_init(struct semaphore* p_sema, uint8_t value);
void sema_down(struct semaphore* p_sema);
void sema_up(struct semaphore* p_sema);

void lock_init(struct lock* plock);
void lock_acquire(struct lock* plock);
void lock_release(struct lock* plock);

#endif