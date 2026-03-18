#include "sched.h"
#include "thread.h"
#include "rbtree.h"
#include "stdint.h"
#include "global.h"
#include "debug.h"
#include "interrupt.h"
#include "print.h"
#include "string.h"
#include "process.h"

/* Nice 值到权重的映射表（Linux 内核标准值）
 * nice 值范围：-20 到 19
 * 数组索引 = nice + 20
 * nice = 0 对应权重 1024（基准值）
 */
const uint32_t sched_prio_to_weight[40] = {
    /* -20 */ 88761, 71755, 56483, 46273, 36291,
    /* -15 */ 29154, 23254, 18705, 14949, 11916,
    /* -10 */  9548,  7620,  6100,  4904,  3906,
    /*  -5 */  3121,  2501,  1991,  1586,  1277,
    /*   0 */  1024,   820,   655,   526,   423,
    /*   5 */   335,   272,   215,   172,   137,
    /*  10 */   110,    87,    70,    56,    45,
    /*  15 */    36,    29,    23,    18,    15,
};

/* CFS 运行队列（全局） */
static struct cfs_rq cfs_rq;

/* 当前正在运行的线程 */
struct task_struct* current_thread = NULL;

/* 初始化 CFS 调度器 */
void sched_init(void) {
    cfs_rq.tasks_timeline.rb_node = NULL;
    cfs_rq.rb_leftmost = NULL;
    cfs_rq.min_vruntime = 0;
    cfs_rq.nr_running = 0;
    put_str("CFS scheduler initialized\n");
}

/* Nice 值转权重 */
uint32_t nice_to_weight(int8_t nice) {
    /* nice 值限制在 -20 到 19 */
    if (nice < -20) nice = -20;
    if (nice > 19) nice = 19;
    return sched_prio_to_weight[nice + 20];
}

/* 计算 vruntime 增量
 * vruntime_delta = delta_exec * NICE_0_LOAD / weight
 * NICE_0_LOAD = 1024（nice=0 的权重）
 * 
 * 使用定点数提高精度，同时确保至少增长 1
 */
void calc_vruntime_delta(struct task_struct* thread, uint64_t delta_exec) {
    /* 防止除零 */
    if (thread->weight == 0) thread->weight = 1024;
    
    /* 使用定点数计算，提高精度
     * 放大 256 倍（8位小数精度）
     * delta_vruntime = (delta_exec * 1024 * 256) / weight
     */
    uint32_t delta_32 = (uint32_t)delta_exec;
    uint32_t scaled_delta = delta_32 << 18;  // * (1024 * 256)
    uint32_t delta_vruntime = scaled_delta / thread->weight;
    
    /* 确保至少增长 1，避免高优先级线程"卡住" */
    if (delta_vruntime == 0) delta_vruntime = 1;
    
    thread->vruntime += delta_vruntime;
}

/* 更新当前线程的 vruntime */
void update_curr(struct task_struct* curr) {
    if (!curr) return;
    
    uint64_t now = curr->elapsed_ticks;  // 简化：用 ticks 作为时间
    uint64_t delta_exec = now - curr->exec_start;
    
    if (delta_exec > 0) {
        calc_vruntime_delta(curr, delta_exec);
        curr->exec_start = now;
        curr->sum_exec_runtime += delta_exec;
        
        /* 更新队列的 min_vruntime (单调递增) */
        uint64_t min_vruntime = curr->vruntime;  // 默认使用当前线程的 vruntime
        
        /* 如果就绪队列非空,比较队列最小值 */
        if (cfs_rq.rb_leftmost) {
            struct task_struct* leftmost = rb_entry(cfs_rq.rb_leftmost,
                                                   struct task_struct, rb_node);
            /* 取当前线程和队列最小值中的较小者 */
            if (leftmost->vruntime < min_vruntime) {
                min_vruntime = leftmost->vruntime;
            }
        }
        
        /* 确保 min_vruntime 单调递增 (不倒退)
         * 取新计算的 min_vruntime 和旧值中的较大者 */
        if (min_vruntime < cfs_rq.min_vruntime) {
            min_vruntime = cfs_rq.min_vruntime;
        }
        cfs_rq.min_vruntime = min_vruntime;
    }
}

/* 将线程加入 CFS 就绪队列（红黑树） */
void enqueue_task(struct task_struct* thread) {
    struct rb_node** link = &cfs_rq.tasks_timeline.rb_node;
    struct rb_node* parent = NULL;
    struct task_struct* entry;
    bool leftmost = true;
    
    /* 新线程的 vruntime 至少等于队列的 min_vruntime */
    if (thread->vruntime < cfs_rq.min_vruntime) {
        thread->vruntime = cfs_rq.min_vruntime;
    }
    
    /* 在红黑树中找到插入位置 */
    while (*link) {
        parent = *link;
        entry = rb_entry(parent, struct task_struct, rb_node);
        
        if (thread->vruntime < entry->vruntime) {
            link = &parent->rb_left;
        } else {
            link = &parent->rb_right;
            leftmost = false;
        }
    }
    
    /* 更新 leftmost */
    if (leftmost) {
        cfs_rq.rb_leftmost = &thread->rb_node;
    }
    
    /* 插入红黑树 */
    rb_link_node(&thread->rb_node, parent, link);
    rb_insert_color(&thread->rb_node, &cfs_rq.tasks_timeline);
    
    cfs_rq.nr_running++;
}

/* 将线程从 CFS 就绪队列移除 */
void dequeue_task(struct task_struct* thread) {
    /* 如果是最左节点，需要更新 */
    if (cfs_rq.rb_leftmost == &thread->rb_node) {
        struct rb_node* next = rb_next(&thread->rb_node);
        cfs_rq.rb_leftmost = next;
    }
    
    /* 从红黑树中删除 */
    rb_erase(&thread->rb_node, &cfs_rq.tasks_timeline);
    cfs_rq.nr_running--;
}

/* 选择下一个要执行的线程（vruntime 最小） */
struct task_struct* pick_next_task(void) {
    if (!cfs_rq.rb_leftmost) {
        return NULL;  // 没有就绪线程
    }
    
    struct task_struct* next = rb_entry(cfs_rq.rb_leftmost, 
                                       struct task_struct, rb_node);
    return next;
}

/* 调度函数 */
void schedule(void) {
    ASSERT(intr_get_status() == INTR_OFF);
    
    struct task_struct* prev = current_thread;
    struct task_struct* next = NULL;
    
    /* 更新当前线程的 vruntime */
    if (prev) {
        update_curr(prev);
        
        /* 如果当前线程还在运行状态，重新加入就绪队列 */
        if (prev->status == TASK_RUNNING) {
            prev->status = TASK_READY;
            enqueue_task(prev);
        }
    }
    
    /* 选择下一个线程 */
    next = pick_next_task();
    
    if (!next) {
        /* 就绪队列为空 */
        extern struct task_struct* idle_thread;
        if (prev == idle_thread) {
            /* idle 线程正在阻塞自己，但没有其他任务可运行，
             * 直接让 idle 继续运行（执行 hlt 指令节省 CPU） */
            idle_thread->status = TASK_RUNNING;
            current_thread = idle_thread;
            return;
        }
        /* 非 idle 线程调度时队列为空，唤醒 idle 线程 */
        thread_unblock(idle_thread);
        next = pick_next_task();
        if (!next) {
            /* 理论上不应到达此处 */
            if (prev) { prev->status = TASK_RUNNING; }
            return;
        }
    }
    
    /* 从就绪队列移除 */
    dequeue_task(next);
    next->status = TASK_RUNNING;
    next->exec_start = next->elapsed_ticks;
    
    /* 激活任务页表等（支持用户进程） */
    process_activate(next);
    
    /* 如果切换到不同的线程 */
    if (next != prev) {
        current_thread = next;
        switch_to(prev, next);  // 上下文切换
    }
}
