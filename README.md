# LinkOS

[English](README_EN.md)

一个从零开始编写的 32 位 x86 操作系统，参考书籍《操作系统真象还原》等，运行在 Bochs 模拟器上。

## 特性

- **完整启动流程**：MBR → Loader（实模式 → 保护模式 → 分页 → ELF 加载）
- **内存管理**：物理/虚拟位图、二级页表、自映射、Arena 小对象分配器（7 种规格：16~1024B）
- **中断系统**：8259A PIC、129 项 IDT、`int 0x80` 系统调用入口
- **CFS 调度器**：基于红黑树的完全公平调度，使用 Linux 标准权重表
- **线程与进程**：内核线程 + Ring 3 用户进程、单 TSS 的 esp0 切换
- **同步原语**：信号量 + 可重入互斥锁
- **类 ext2 文件系统**：超级块 + 位图 + inode 三级间接索引、4KB 块、最多 4096 文件
- **交互式 Shell**：12 个内建命令（ls, cd, cat, echo, mkdir, rm 等），支持 `>` 重定向
- **设备驱动**：键盘（扫描码转换 + 环形缓冲）、8253 PIT 时钟（100Hz）、IDE 硬盘、VGA 文本模式

## 项目结构

```
LinkOS/
├── bochs/                   # Bochs 模拟器运行环境
│   ├── bochsrc.txt          # Bochs 配置文件
│   └── disk/                # 磁盘镜像目录
│       ├── hd60M.img        # 主盘（系统盘，含 MBR/Loader/Kernel）
│       └── hd80M.img        # 从盘（文件系统分区）
│
└── code/                    # 全部源代码（~9400 行，66 个文件）
    ├── mbr.asm              # 主引导记录（512 字节，加载 Loader）
    ├── loader.asm           # 加载器（实模式→保护模式→分页→加载内核）
    ├── include/boot.inc     # 引导阶段共享常量（地址、GDT 属性等）
    ├── makefile             # 构建系统
    │
    ├── kernel/              # 内核核心
    │   ├── main.c           # 内核入口 main()
    │   ├── init.c/h         # 子系统初始化调度
    │   ├── global.h         # GDT 段选择子、全局宏
    │   ├── interrupt.c/h    # IDT + 8259A PIC + 中断开关
    │   ├── kernel.asm       # 中断入口表 + syscall 入口 + intr_exit
    │   ├── memory.c/h       # 物理/虚拟内存管理 + 页表 + Arena 分配器
    │   ├── sched.c/h        # CFS 调度器（红黑树）
    │   ├── switch.asm       # 线程上下文切换（保存/恢复 esp）
    │   └── syscall.c/h      # syscall 分发表 + sys_malloc/sys_free
    │
    ├── thread/              # 线程子系统
    │   ├── thread.c/h       # PCB/TCB + 线程创建/阻塞/唤醒 + idle 线程
    │   └── sync.c/h         # 信号量 + 可重入锁
    │
    ├── userprog/            # 用户进程
    │   ├── process.c/h      # 用户进程创建 + 页目录 + Ring 3 切换
    │   └── tss.c/h          # TSS 管理（esp0 更新）
    │
    ├── device/              # 设备驱动
    │   ├── keyboard.c/h     # 键盘中断处理 + 扫描码映射
    │   ├── timer.c/h        # 8253 PIT 初始化 + 时钟中断（100Hz）
    │   ├── console.c/h      # 控制台输出锁
    │   └── ioqueue.c/h      # 生产者-消费者环形缓冲区
    │
    ├── fs/                  # 文件系统
    │   ├── fs.c/h           # 文件系统初始化/挂载 + 16 个 sys_* 系统调用
    │   ├── super_block.h    # 超级块结构（512 字节）
    │   ├── inode.c/h        # inode 操作 + 三级间接索引
    │   ├── dir.c/h          # 目录操作（搜索/创建/删除目录项）
    │   └── file.c/h         # 文件操作（全局打开文件表 + 读写）
    │
    ├── shell/               # 交互式 Shell
    │   └── shell.c/h        # 12 个内建命令 + 命令解析 + 路径处理
    │
    └── lib/                 # 库函数
        ├── string.c/h       # memset/memcpy/strcmp/strcpy 等
        ├── stdint.h         # 整数类型定义
        ├── stdarg.h         # 可变参数宏
        ├── kernel/          # 内核态库
        │   ├── print.asm/h  # VGA 文本输出（put_char/put_str/put_int/cls_screen）
        │   ├── bitmap.c/h   # 位图操作
        │   ├── list.c/h     # 双向链表
        │   ├── rbtree.c/h   # 红黑树（CFS 调度用）
        │   ├── debug.c/h    # ASSERT + PANIC 宏
        │   └── io.h         # 端口 I/O 内联汇编（inb/outb/insw/outsw）
        └── usr/             # 用户态库
            ├── stdio.c/h    # printf/sprintf
            └── syscall.c/h  # 用户态系统调用封装
```

## 系统启动流程

```
BIOS (0x7C00)
  │
  ▼
MBR (扇区 0) ─── 从硬盘读取 Loader 到 0x900
  │
  ▼
Loader (扇区 2~5)
  ├── 检测物理内存（E820/E801/0x88）
  ├── 建立 GDT，开启 A20
  ├── 设置 CR0.PE → 进入保护模式
  ├── 初始化页目录/页表 → 开启分页
  ├── 从磁盘读取 kernel.bin 到 0x70000
  ├── 解析 ELF，拷贝段到 0xc0001500
  └── 跳转到 KERNEL_ENTRY_POINT
        │
        ▼
Kernel main() (0xc0001500)
  ├── init_all()  ← 初始化所有子系统
  │   ├── idt_init()       中断描述符表 + 8259A
  │   ├── mem_init()       内存池 + Arena
  │   ├── thread_init()    PCB 链表 + CFS + idle 线程
  │   ├── timer_init()     PIT 100Hz
  │   ├── console_init()   控制台锁
  │   ├── keyboard_init()  键盘中断
  │   ├── tss_init()       TSS + 用户段描述符
  │   └── filesys_init()   文件系统挂载
  ├── cls_screen()
  ├── thread_start("shell") ← 启动 Shell 内核线程
  └── intr_enable() → 空循环
```

## 磁盘布局

### 主盘 (hd60M.img)

| 扇区 | 内容 |
|------|------|
| 0 | MBR（512 字节，末尾 `0x55AA`） |
| 2~5 | Loader（4 扇区） |
| 9~208 | Kernel（200 扇区，最大 100KB） |

### 从盘分区 (hd80M.img) — 文件系统布局

| 块号 | 内容 |
|------|------|
| 0 | 引导扇区 + 超级块（512B）|
| 1~N | 块位图 |
| N+1 | inode 位图（1 块，4096 个 inode）|
| ... | inode 表（~86 块）|
| ... | 数据区 |

- **块大小**：4096 字节（8 扇区）
- **魔数**：`0x19590321`
- **inode 索引**：12 直接 + 1 一级间接 + 1 二级间接 + 1 三级间接

## 内存布局

| 地址范围 | 用途 |
|----------|------|
| `0x00000000 ~ 0x000003FF` | 中断向量表 (IVT) |
| `0x00000900` | Loader 加载位置 |
| `0x00007C00` | MBR 加载位置 |
| `0x00070000` | Kernel 临时加载位置 |
| `0x00100000` | 页目录表（1MB 处）|
| `0xc0000000 ~` | 内核虚拟地址空间 |
| `0xc0001500` | 内核入口点（虚拟）|
| `0xc0100000` | 内核堆起始地址 |
| `0xc009a000` | 位图管理区 |

## Shell 命令

| 命令 | 说明 | 示例 |
|------|------|------|
| `help` | 显示帮助信息 | `help` |
| `clear` / `cls` | 清屏 | `clear` |
| `pwd` | 显示当前工作目录 | `pwd` |
| `cd <path>` | 切换目录 | `cd /home` |
| `ls [path]` | 列出目录内容 | `ls /` |
| `mkdir <path>` | 创建目录 | `mkdir /test` |
| `rmdir <path>` | 删除空目录 | `rmdir /test` |
| `touch <file>` | 创建空文件 | `touch /hello.txt` |
| `rm <file>` | 删除文件 | `rm /hello.txt` |
| `cat <file>` | 显示文件内容 | `cat /readme` |
| `echo <text> [> file]` | 输出文本 / 写入文件 | `echo hello > /greet.txt` |
| `stat <path>` | 查看文件/目录信息 | `stat /home` |

## 系统调用

通过 `int 0x80` 触发，eax 传递调用号，ebx/ecx/edx 传递参数：

| 调用号 | 名称 | 说明 |
|--------|------|------|
| 0 | `getpid` | 获取当前进程 PID |
| 1 | `write` | 写文件 / 控制台输出 |
| 2 | `malloc` | 分配堆内存 |
| 3 | `free` | 释放堆内存 |
| 4 | `open` | 打开/创建文件 |
| 5 | `close` | 关闭文件描述符 |
| 6 | `read` | 读文件 / 键盘输入 |
| 7 | `lseek` | 移动文件偏移量 |
| 8 | `unlink` | 删除文件 |
| 9 | `mkdir` | 创建目录 |
| 10 | `opendir` | 打开目录 |
| 11 | `closedir` | 关闭目录 |
| 12 | `readdir` | 读取目录项 |
| 13 | `rewinddir` | 重置目录读取位置 |
| 14 | `rmdir` | 删除目录 |
| 15 | `getcwd` | 获取当前工作目录 |
| 16 | `chdir` | 切换工作目录 |
| 17 | `stat` | 获取文件信息 |

## 构建与运行

### 环境要求

- **GCC** (支持 `-m32`)
- **NASM** (汇编器)
- **LD** (GNU linker, 支持 `elf_i386`)
- **Bochs** (x86 模拟器)
- 32 位 C 库：`sudo apt install gcc-multilib`

### 编译

```bash
cd code
make clean && make
```

`make` 会自动完成：
1. 编译所有 `.c`/`.asm` 源文件为 `.o`
2. 链接生成 `kernel.bin`（入口 `0xc0001500`）
3. 汇编生成 `mbr.bin` 和 `loader.bin`
4. 用 `dd` 将三者写入 `../bochs/disk/hd60M.img`

### 运行

```bash
cd ../bochs
./bin/bochs -f bochsrc.txt -q
```

### 调试

在 Bochs 中使用调试命令：

```
bp 0x7c00              # MBR 入口断点
bp 0xc0001500          # 内核入口断点
c                      # 继续执行
s                      # 单步
n                      # 跳过 call
r                      # 查看寄存器
x /20xb 0x70000        # 查看内存
u /10 0xc0001500       # 反汇编
```

## 技术细节

### 编译选项

| 选项 | 说明 |
|------|------|
| `-m32` | 生成 32 位代码 |
| `-fno-builtin` | 禁用 GCC 内置函数优化 |
| `-mno-sse -mno-sse2` | 禁用 SSE 指令（未在 CR4 中启用 OSFXSR）|
| `-Wall -W` | 开启所有警告 |
| `-Ttext 0xc0001500` | 内核入口地址 |
| `-e main` | 指定入口符号 |

### GDT 段布局

| 索引 | 选择子 | 说明 |
|------|--------|------|
| 0 | `0x00` | 空描述符 |
| 1 | `0x08` | 内核代码段 (DPL=0) |
| 2 | `0x10` | 内核数据段 (DPL=0) |
| 3 | `0x18` | 显存段 (DPL=0) |
| 4 | `0x20` | TSS (DPL=0) |
| 5 | `0x2B` | 用户代码段 (DPL=3) |
| 6 | `0x33` | 用户数据段 (DPL=3) |

### 调度器

采用 Linux CFS（完全公平调度器）算法：
- 使用**红黑树**按 `vruntime` 排序所有就绪线程
- 每次调度选择 `vruntime` 最小的线程运行
- `vruntime` 增量 = 实际运行时间 × 1024 / 权重
- 权重由 nice 值查表确定（nice=0 → weight=1024）
- 时钟中断频率 100Hz，调度延迟 30ms，最小粒度 10ms

## 许可证

本项目基于 [MIT License](LICENSE) 开源。
