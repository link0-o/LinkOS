#include "print.h"
#include "init.h"
#include "memory.h"
#include "thread.h"
#include "sched.h"
#include "interrupt.h"
#include "process.h"
#include "console.h"
#include "syscall.h"
#include "stdio.h"
#include "string.h"
#include "shell.h"

int main(void) {
    put_str("I am kernel\n");
    init_all();

    /* 清屏, 启动 shell */
    cls_screen();
    thread_start("shell", 10, my_shell, NULL);

    intr_enable();
    while(1);
    return 0;
}
