#include "ioqueue.h"
#include "interrupt.h"
#include "global.h"
#include "debug.h"

/* 初始化io队列 */
void ioqueue_init(ioqueue_t* ioq) {
    lock_init(&ioq->lock);
    sema_init(&ioq->has_data, 0);
    sema_init(&ioq->has_space, bufsize);
    ioq->head = 0;
    ioq->tail = 0;
}

/* 返回 pos 在缓冲区中的下一个位置值 */
static int32_t next_pos(int32_t pos){
    return (pos+1) % bufsize;
}

/* 判断队列是否已满 */
bool ioq_full(struct ioqueue* ioq){
    ASSERT(intr_get_status() == INTR_OFF);
    return next_pos(ioq->head) == ioq->tail;
}

/* 判断队列是否已空 */
bool ioq_empty(struct ioqueue* ioq){
    ASSERT(intr_get_status() == INTR_OFF);
    return ioq->head == ioq->tail;
}

/* 消费者从 ioq 队列中获取一个字符 */
char ioq_getchar(struct ioqueue* ioq){

    sema_down(&ioq->has_data);  // P 操作: has_data--
    
    /* 获取锁,保护缓冲区访问 */
    lock_acquire(&ioq->lock);
    
    ASSERT(!ioq_empty(ioq));
    
    char byte = ioq->buf[ioq->tail];
    ioq->tail = next_pos(ioq->tail);
    
    lock_release(&ioq->lock);
    
    sema_up(&ioq->has_space);  // V 操作: has_space++
    
    return byte;
}

/* 生产者向 ioq 队列中存入一个字符 */
void ioq_putchar(struct ioqueue* ioq, char byte){

    sema_down(&ioq->has_space);  // P 操作: has_space--
    
    /* 获取锁,保护缓冲区访问 */
    lock_acquire(&ioq->lock);
    
    ASSERT(!ioq_full(ioq));
    
    /* 向缓冲区写入数据 */
    ioq->buf[ioq->head] = byte;
    ioq->head = next_pos(ioq->head);
    
    lock_release(&ioq->lock);
    
    sema_up(&ioq->has_data);  // V 操作: has_data++
}