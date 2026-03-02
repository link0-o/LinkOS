#ifndef __DEVICE_IOQUEUE_H
#define __DEVICE_IOQUEUE_H

#include "stdint.h"
#include "thread.h"
#include "sync.h"

#define bufsize 64

typedef struct ioqueue{
    // 生产者-消费者模型缓冲区
    struct lock lock;               // 保护缓冲区的互斥锁
    
    struct semaphore has_data;      // 记录缓冲区中有多少数据(消费者等待)
    struct semaphore has_space;     // 记录缓冲区中有多少空位(生产者等待)
    
    char buf[bufsize];              // 环形缓冲区
    uint32_t head;                  // 写入位置(生产者)
    uint32_t tail;                  // 读取位置(消费者)
} ioqueue_t;

void ioqueue_init(ioqueue_t* queue);
bool ioq_full(ioqueue_t* queue);
bool ioq_empty(ioqueue_t* queue);
void ioq_putchar(ioqueue_t* queue, char c);
char ioq_getchar(ioqueue_t* queue);

#endif // IOQUEUE_H