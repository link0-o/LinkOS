#include "shell.h"
#include "stdint.h"
#include "global.h"
#include "string.h"
#include "syscall.h"
#include "stdio-kernel.h"
#include "console.h"
#include "print.h"
#include "fs.h"
#include "file.h"
#include "dir.h"
#include "ioqueue.h"

extern struct ioqueue kbd_buf;   /* keyboard.c 中定义 */

/* ============================================================
 *                      工 具 函 数
 * ============================================================ */

/* 从键盘缓冲区读取一行到 buf, 以回车结束, 支持退格
 * 返回实际有效字符数 (不含结尾 '\0') */
static uint32_t readline(char* buf, uint32_t buf_size) {
    uint32_t pos = 0;
    while (1) {
        char ch = ioq_getchar(&kbd_buf);

        if (ch == '\n' || ch == '\r') {
            /* 回车已被键盘中断回显, 这里只做换行处理 */
            buf[pos] = '\0';
            return pos;
        } else if (ch == '\b') {
            /* 退格: 键盘中断已处理显示, 只需回退 pos */
            if (pos > 0) {
                pos--;
            }
        } else {
            if (pos < buf_size - 1) {
                buf[pos++] = ch;
            }
            /* 缓冲区满则丢弃 (字符已回显, 无法撤销, 但命令被截断) */
        }
    }
}

/* 将命令行字符串按空格拆分为 argv[], 返回 argc
 * 注意: 原字符串会被修改 (空格替换为 '\0') */
static int32_t cmd_parse(char* cmd_str, char* argv[], int32_t max_argc) {
    int32_t argc = 0;
    char* p = cmd_str;

    while (*p != '\0' && argc < max_argc) {
        /* 跳过前导空格 */
        while (*p == ' ') p++;
        if (*p == '\0') break;

        argv[argc++] = p;

        /* 前进到下一个空格或结尾 */
        while (*p != '\0' && *p != ' ') p++;
        if (*p == ' ') {
            *p = '\0';
            p++;
        }
    }
    return argc;
}

/* 打印提示符: [LinkOS /当前路径]$ */
static void print_prompt(void) {
    char cwd[MAX_PATH_LEN] = {0};
    sys_getcwd(cwd, MAX_PATH_LEN);
    printk("[LinkOS %s]$ ", cwd);
}

/* 将相对路径拼接到绝对路径
 * 如果 path 以 '/' 开头, 直接拷贝; 否则拼接 cwd + "/" + path
 * 结果写入 abs_path, 调用者保证缓冲区足够大 */
static void make_abs_path(const char* path, char* abs_path) {
    if (path[0] == '/') {
        strcpy(abs_path, path);
    } else {
        sys_getcwd(abs_path, MAX_PATH_LEN);
        if (!(abs_path[0] == '/' && abs_path[1] == '\0')) {
            /* cwd 不是根目录, 加 '/' */
            strcat(abs_path, "/");
        }
        strcat(abs_path, path);
    }
}

/* ============================================================
 *                    内 建 命 令 实 现
 * ============================================================ */

/* ---------- help ---------- */
static void builtin_help(void) {
    printk("LinkOS Shell - Built-in Commands:\n");
    printk("  help              - Show this help message\n");
    printk("  clear / cls       - Clear screen\n");
    printk("  pwd               - Print working directory\n");
    printk("  cd <path>         - Change directory\n");
    printk("  ls [path]         - List directory contents\n");
    printk("  mkdir <path>      - Create directory\n");
    printk("  rmdir <path>      - Remove empty directory\n");
    printk("  touch <path>      - Create empty file\n");
    printk("  rm <path>         - Delete file\n");
    printk("  cat <path>        - Display file contents\n");
    printk("  echo <text>       - Print text (use > file to write)\n");
    printk("  stat <path>       - Show file/directory info\n");
}

/* ---------- clear ---------- */
static void builtin_clear(void) {
    cls_screen();
}

/* ---------- pwd ---------- */
static void builtin_pwd(void) {
    char cwd[MAX_PATH_LEN] = {0};
    if (sys_getcwd(cwd, MAX_PATH_LEN) != NULL) {
        printk("%s\n", cwd);
    } else {
        printk("pwd: failed to get current directory\n");
    }
}

/* ---------- cd ---------- */
static void builtin_cd(int argc, char* argv[]) {
    if (argc < 2) {
        /* cd 无参数: 回到根目录 */
        sys_chdir("/");
        return;
    }
    char abs_path[MAX_PATH_LEN] = {0};
    make_abs_path(argv[1], abs_path);
    if (sys_chdir(abs_path) == -1) {
        printk("cd: %s: No such directory\n", argv[1]);
    }
}

/* ---------- ls ---------- */
static void builtin_ls(int argc, char* argv[]) {
    char abs_path[MAX_PATH_LEN] = {0};
    if (argc < 2) {
        sys_getcwd(abs_path, MAX_PATH_LEN);
    } else {
        make_abs_path(argv[1], abs_path);
    }

    struct dir* dir = sys_opendir(abs_path);
    if (dir == NULL) {
        printk("ls: cannot access '%s': No such directory\n",
               argc < 2 ? "." : argv[1]);
        return;
    }

    sys_rewinddir(dir);
    struct dir_entry* de = NULL;
    while ((de = sys_readdir(dir)) != NULL) {
        /* 跳过 . 和 .. */
        if (!strcmp(de->filename, ".") || !strcmp(de->filename, "..")) {
            continue;
        }
        if (de->f_type == FT_DIRECTORY) {
            printk("%s/  ", de->filename);
        } else {
            printk("%s  ", de->filename);
        }
    }
    printk("\n");
    sys_closedir(dir);
}

/* ---------- mkdir ---------- */
static void builtin_mkdir(int argc, char* argv[]) {
    if (argc < 2) {
        printk("mkdir: missing operand\n");
        return;
    }
    char abs_path[MAX_PATH_LEN] = {0};
    make_abs_path(argv[1], abs_path);
    if (sys_mkdir(abs_path) == -1) {
        printk("mkdir: cannot create directory '%s'\n", argv[1]);
    }
}

/* ---------- rmdir ---------- */
static void builtin_rmdir(int argc, char* argv[]) {
    if (argc < 2) {
        printk("rmdir: missing operand\n");
        return;
    }
    char abs_path[MAX_PATH_LEN] = {0};
    make_abs_path(argv[1], abs_path);
    if (sys_rmdir(abs_path) == -1) {
        printk("rmdir: failed to remove '%s'\n", argv[1]);
    }
}

/* ---------- touch ---------- */
static void builtin_touch(int argc, char* argv[]) {
    if (argc < 2) {
        printk("touch: missing file operand\n");
        return;
    }
    char abs_path[MAX_PATH_LEN] = {0};
    make_abs_path(argv[1], abs_path);
    int32_t fd = sys_open(abs_path, O_CREAT | O_RDWR);
    if (fd == -1) {
        printk("touch: cannot create '%s'\n", argv[1]);
    } else {
        sys_close(fd);
    }
}

/* ---------- rm ---------- */
static void builtin_rm(int argc, char* argv[]) {
    if (argc < 2) {
        printk("rm: missing operand\n");
        return;
    }
    char abs_path[MAX_PATH_LEN] = {0};
    make_abs_path(argv[1], abs_path);
    if (sys_unlink(abs_path) == -1) {
        printk("rm: cannot remove '%s'\n", argv[1]);
    }
}

/* ---------- cat ---------- */
static void builtin_cat(int argc, char* argv[]) {
    if (argc < 2) {
        printk("cat: missing file operand\n");
        return;
    }
    char abs_path[MAX_PATH_LEN] = {0};
    make_abs_path(argv[1], abs_path);
    int32_t fd = sys_open(abs_path, O_RDONLY);
    if (fd == -1) {
        printk("cat: %s: No such file\n", argv[1]);
        return;
    }

    /* 获取文件大小 */
    struct stat file_stat;
    sys_stat(abs_path, &file_stat);

    if (file_stat.st_filetype == FT_DIRECTORY) {
        printk("cat: %s: Is a directory\n", argv[1]);
        sys_close(fd);
        return;
    }

    /* 逐块读取并输出 */
    char buf[1025];  /* 1KB + '\0' */
    int32_t bytes_read;
    while ((bytes_read = sys_read(fd, buf, 1024)) > 0) {
        buf[bytes_read] = '\0';
        printk("%s", buf);
    }
    sys_close(fd);
}

/* ---------- echo ----------
 * 支持:
 *   echo hello world          → 输出到屏幕
 *   echo hello world > file   → 写入文件 (覆盖)
 */
static void builtin_echo(int argc, char* argv[]) {
    if (argc < 2) {
        printk("\n");
        return;
    }

    /* 检查是否有 '>' 重定向 */
    int32_t redir_idx = -1;
    int32_t i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], ">")) {
            redir_idx = i;
            break;
        }
    }

    if (redir_idx != -1) {
        /* 有重定向 */
        if (redir_idx + 1 >= argc) {
            printk("echo: syntax error: expected filename after '>'\n");
            return;
        }

        /* 拼接 '>' 之前的文本 */
        char text[CMD_LEN] = {0};
        for (i = 1; i < redir_idx; i++) {
            strcat(text, argv[i]);
            if (i < redir_idx - 1) strcat(text, " ");
        }
        strcat(text, "\n");

        char abs_path[MAX_PATH_LEN] = {0};
        make_abs_path(argv[redir_idx + 1], abs_path);

        /* 打开文件 (不存在则创建), 截断写入 */
        int32_t fd = sys_open(abs_path, O_CREAT | O_WRONLY);
        if (fd == -1) {
            printk("echo: cannot open '%s'\n", argv[redir_idx + 1]);
            return;
        }
        /* 先 seek 到开头 (覆盖) */
        sys_lseek(fd, 0, SEEK_SET);
        sys_write(fd, text, strlen(text));
        sys_close(fd);
    } else {
        /* 无重定向, 输出到屏幕 */
        for (i = 1; i < argc; i++) {
            printk("%s", argv[i]);
            if (i < argc - 1) printk(" ");
        }
        printk("\n");
    }
}

/* ---------- stat ---------- */
static void builtin_stat(int argc, char* argv[]) {
    if (argc < 2) {
        printk("stat: missing operand\n");
        return;
    }
    char abs_path[MAX_PATH_LEN] = {0};
    make_abs_path(argv[1], abs_path);
    struct stat st;
    if (sys_stat(abs_path, &st) == -1) {
        printk("stat: cannot stat '%s': No such file or directory\n", argv[1]);
        return;
    }
    printk("  File: %s\n", argv[1]);
    printk("  inode: %d\n", st.st_ino);
    printk("  size: %d bytes\n", st.st_size);
    printk("  type: %s\n",
           st.st_filetype == FT_REGULAR ? "regular file" :
           st.st_filetype == FT_DIRECTORY ? "directory" : "unknown");
}

/* ============================================================
 *                     Shell 主 循 环
 * ============================================================ */

void my_shell(void* arg UNUSED) {
    char cmd_line[CMD_LEN] = {0};
    char* argv[MAX_ARG_NR] = {NULL};

    printk("\n========================================\n");
    printk("  Welcome to LinkOS Shell!\n");
    printk("  Type 'help' for available commands.\n");
    printk("========================================\n\n");

    while (1) {
        print_prompt();

        memset(cmd_line, 0, CMD_LEN);
        uint32_t len = readline(cmd_line, CMD_LEN);
        if (len == 0) {
            /* 空行, 直接下一轮 */
            continue;
        }

        /* 解析参数 */
        memset(argv, 0, sizeof(argv));
        int32_t argc = cmd_parse(cmd_line, argv, MAX_ARG_NR);
        if (argc == 0) continue;

        /* 匹配并执行内建命令 */
        if (!strcmp(argv[0], "help")) {
            builtin_help();
        } else if (!strcmp(argv[0], "clear") || !strcmp(argv[0], "cls")) {
            builtin_clear();
        } else if (!strcmp(argv[0], "pwd")) {
            builtin_pwd();
        } else if (!strcmp(argv[0], "cd")) {
            builtin_cd(argc, argv);
        } else if (!strcmp(argv[0], "ls")) {
            builtin_ls(argc, argv);
        } else if (!strcmp(argv[0], "mkdir")) {
            builtin_mkdir(argc, argv);
        } else if (!strcmp(argv[0], "rmdir")) {
            builtin_rmdir(argc, argv);
        } else if (!strcmp(argv[0], "touch")) {
            builtin_touch(argc, argv);
        } else if (!strcmp(argv[0], "rm")) {
            builtin_rm(argc, argv);
        } else if (!strcmp(argv[0], "cat")) {
            builtin_cat(argc, argv);
        } else if (!strcmp(argv[0], "echo")) {
            builtin_echo(argc, argv);
        } else if (!strcmp(argv[0], "stat")) {
            builtin_stat(argc, argv);
        } else {
            printk("%s: command not found\n", argv[0]);
        }
    }
}
