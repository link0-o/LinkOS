#ifndef __KERNEL_SCHED_H
#define __KERNEL_SCHED_H

#include "thread.h"
#include "rbtree.h"
#include "stdint.h"

/* CFS 运行队列 */
struct cfs_rq {
    struct rb_root tasks_timeline;  // 红黑树根（按 vruntime 排序）
    struct rb_node* rb_leftmost;    // 最左节点（vruntime 最小的线程）
    uint64_t min_vruntime;          // 队列最小 vruntime（单调递增）
    uint32_t nr_running;            // 就绪队列中的线程数
};

/* Nice 值到权重的映射表（Linux 标准值） */
extern const uint32_t sched_prio_to_weight[40];

/* CFS 调度器函数 */
void sched_init(void);                              // 初始化调度器
void enqueue_task(struct task_struct* thread);      // 将线程加入就绪队列
void dequeue_task(struct task_struct* thread);      // 将线程从就绪队列移除
struct task_struct* pick_next_task(void);           // 选择下一个要执行的线程
void update_curr(struct task_struct* curr);         // 更新当前线程的 vruntime
void schedule(void);                                // 调度函数

/* 辅助函数 */
uint32_t nice_to_weight(int8_t nice);               // nice 值转权重
void calc_vruntime_delta(struct task_struct* thread, uint64_t delta_exec);  // 计算 vruntime 增量

/* 获取当前正在运行的线程 */
extern struct task_struct* current_thread;

#endif
