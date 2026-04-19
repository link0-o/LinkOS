#ifndef __USERPROG_PROCESS_H
#define __USERPROG_PROCESS_H

#include "thread.h"
#include "stdint.h"

#define USER_STACK3_VADDR  (0xc0000000 - 0x1000)
#define USER_VADDR_START   0x8048000    /* 用户进程起始虚拟地址 */

void start_process(void* filename_);            // 构建用户进程初始上下文信息
void page_dir_activate(struct task_struct* p_thread);       // 激活页表
void process_activate(struct task_struct* p_thread);        // 激活进程（切换页表、更新 tss esp0）
uint32_t* create_page_dir(void);                            // 创建页目录 
void create_user_vaddr_bitmap(struct task_struct* user_prog);       // 创建用户进程虚拟地址位图
void process_execute(void* filename, char* name);           // 创建用户进程

#endif
