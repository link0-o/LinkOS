#ifndef __SHELL_SHELL_H
#define __SHELL_SHELL_H

#include "stdint.h"

/* Shell 输入行最大长度 */
#define CMD_LEN      128

/* 命令参数最大个数 */
#define MAX_ARG_NR   16

/* Shell 主函数 (作为内核线程入口) */
void my_shell(void* arg);

#endif
