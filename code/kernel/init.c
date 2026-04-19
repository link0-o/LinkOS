#include "init.h"
#include "print.h"
#include "interrupt.h"
#include "timer.h"
#include "memory.h"
#include "thread.h"
#include "console.h"
#include "keyboard.h"
#include "tss.h"
#include "ide.h"
#include "fs.h"

/*负责初始化所有模块 */
void init_all(void) {
    put_str("init_all\n");
    idt_init();
    timer_init();   // 初始化时钟 (必须在中断初始化之后)
    mem_init();
    thread_init();  // 初始化线程系统
    console_init(); // 控制台初始化最好放在开中断之前
    keyboard_init();// 键盘初始化
    tss_init();     // TSS 初始化 (为将来的用户进程做准备)
    ide_init();     // 硬盘驱动初始化
    filesys_init(); // 文件系统初始化（格式化 + 挂载）
}