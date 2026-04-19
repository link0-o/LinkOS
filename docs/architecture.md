# LinkOS 架构与代码详解

本文档详细说明 LinkOS 各子系统的设计原理、核心数据结构和关键代码实现。

---

## 目录

1. [引导过程](#1-引导过程)
2. [中断系统](#2-中断系统)
3. [内存管理](#3-内存管理)
4. [线程与进程](#4-线程与进程)
5. [CFS 调度器](#5-cfs-调度器)
6. [同步机制](#6-同步机制)
7. [文件系统](#7-文件系统)
8. [系统调用](#8-系统调用)
9. [设备驱动](#9-设备驱动)
10. [Shell](#10-shell)
11. [汇编模块详解](#11-汇编模块详解)

---

## 1. 引导过程

### 1.1 MBR (`mbr.asm`)

BIOS 将磁盘第 0 扇区（512 字节）加载到物理地址 `0x7C00` 并跳转执行。

**工作流程**：
1. 初始化段寄存器 `DS=ES=SS=0`, `SP=0x7C00`
2. `INT 0x10` 清屏
3. 调用 `rd_disk_m_16` 从扇区 2 读取 4 个扇区到 `0x900`（Loader）
4. `jmp LOADER_BASE_ADDR` 跳转到 Loader

**磁盘读取（实模式 PIO）**：
```
设置扇区数 → 写 LBA 到端口 0x1F3~0x1F6 → 写读命令 0x20 到 0x1F7
→ 轮询 0x1F7 直到 DRQ=1 → insw 从 0x1F0 读数据
```

末尾两字节必须为 `0x55AA`（MBR 签名），否则 BIOS 不识别。

### 1.2 Loader (`loader.asm`)

Loader 是整个启动过程中最复杂的部分（~656 行），完成从 16 位实模式到 32 位保护模式分页模式的全部过渡。

**阶段 1 — 内存检测（实模式）**：
- 优先尝试 INT 0x15 EAX=0xE820（最详细，返回 ARDS 数组）
- 失败则尝试 INT 0x15 AX=0xE801（最大 4GB）
- 再失败尝试 INT 0x15 AH=0x88（最大 64MB）
- 结果存放在 `0x0B08`（`total_mem_bytes`）

**阶段 2 — 进入保护模式**：
1. 开启 A20 地址线（写端口 `0x92`，置位 bit 1）
2. `lgdt` 加载 GDT（起始地址 `0x908`，3 个段描述符）
3. `CR0.PE = 1` → 远跳转 `jmp dword SELECTOR_CODE:p_mode_start`

**阶段 3 — 启用分页**：
1. 在 `0x100000`（1MB）处创建页目录表（PD）
2. PD[0] 和 PD[768] 都指向同一页表（恒等映射低 1MB + 映射到 `0xC0000000`）
3. PD[1023] 指向 PD 自身（**页表自映射**，用于后续通过虚拟地址访问页表）
4. 设置 `CR3 = 0x100000`, `CR0.PG = 1`

**阶段 4 — 加载内核**：
1. `rd_disk_m_32` 从扇区 9 读取 200 扇区到物理地址 `0x70000`
2. `kernel_init` 解析 ELF 格式：遍历 Program Header Table，将每个 `PT_LOAD` 段的 `p_filesz` 字节从 `p_offset` 拷贝到 `p_vaddr`
3. 远跳转到 `KERNEL_ENTRY_POINT (0xc0001500)`

### 1.3 关键地址常量 (`include/boot.inc`)

| 常量 | 值 | 说明 |
|------|----|------|
| `LOADER_BASE_ADDR` | `0x900` | Loader 加载到此地址 |
| `LOADER_START_SECTOR` | `0x2` | Loader 在磁盘的起始扇区 |
| `KERNEL_START_SECTOR` | `0x9` | Kernel 在磁盘的起始扇区 |
| `KERNEL_BIN_BASE_ADDR` | `0x70000` | Kernel ELF 临时加载地址 |
| `KERNEL_ENTRY_POINT` | `0xc0001500` | 内核入口虚拟地址 |
| `PAGE_DIR_TABLE_POS` | `0x100000` | 页目录表物理地址 |

---

## 2. 中断系统

**文件**：`kernel/interrupt.c`, `kernel/kernel.asm`

### 2.1 IDT 初始化

- 共 129 个中断描述符（`IDT_DESC_CNT = 0x81`），覆盖 0x00~0x80
- 0x00~0x1F：CPU 异常（除零、Page Fault、Invalid Opcode 等）
- 0x20~0x2F：外部硬件中断（通过 8259A PIC 重映射）
- 0x80：系统调用入口（**DPL=3**，允许用户态触发）

### 2.2 8259A PIC 编程

```
主片 ICW1~ICW4 → 端口 0x20/0x21
从片 ICW1~ICW4 → 端口 0xA0/0xA1
主片 IRQ 起始向量号 = 0x20（时钟=0x20, 键盘=0x21, 级联=0x22）
从片 IRQ 起始向量号 = 0x28（硬盘=0x2E）
OCW1 开启: IRQ0(时钟), IRQ1(键盘), IRQ2(级联), IRQ14(硬盘)
```

### 2.3 中断入口 (`kernel.asm`)

`VECTOR` 宏为每个中断向量生成一段入口代码：

```asm
; 1. 保存上下文
push ds / es / fs / gs
pushad

; 2. 发送 EOI (如果是外部中断)
mov al, 0x20
out 0xa0, al    ; 从片 EOI
out 0x20, al    ; 主片 EOI

; 3. 调用 C 处理函数
push vec_no
call [idt_table + vec_no*4]

; 4. 恢复上下文
jmp intr_exit   ; popad + pop segments + iretd
```

### 2.4 异常处理

- 通用异常处理 `general_intr_handler`：打印向量号后 `while(1)` 挂起
- Page Fault 专用处理：额外打印 CR2（触发缺页的虚拟地址）

---

## 3. 内存管理

**文件**：`kernel/memory.c/h`

### 3.1 物理内存池

系统总内存从 `0x0B08` 读取（Loader 阶段存放）。扣除低 1MB 后，剩余物理内存对半分为两个池：

| 池 | 用途 |
|----|------|
| `kernel_pool` | 内核态页分配 |
| `user_pool` | 用户进程页分配 |

每个池用**位图**管理：1 bit = 1 页（4KB），位图存放在 `0xc009a000` 起始的虚拟地址。

### 3.2 虚拟地址管理

- 内核虚拟地址池起始：`K_HEAP_START = 0xc0100000`
- 每个用户进程有独立的虚拟地址池（位图在 `userprog_vaddr` 中）
- 用户进程起始虚拟地址：`USER_VADDR_START = 0x8048000`（与 Linux 一致）

### 3.3 页表操作

利用**自映射**机制（PD[1023] → PD 自身），可通过虚拟地址访问任意 PDE/PTE：

```c
// 获取虚拟地址 vaddr 对应的 PTE 虚拟地址
uint32_t* pte_ptr(uint32_t vaddr) {
    return (uint32_t*)(0xffc00000 + ((vaddr & 0xffc00000) >> 10) + PTE_IDX(vaddr) * 4);
}

// 获取虚拟地址 vaddr 对应的 PDE 虚拟地址
uint32_t* pde_ptr(uint32_t vaddr) {
    return (uint32_t*)(0xfffff000 + PDE_IDX(vaddr) * 4);
}
```

**page_table_add** 建立映射时，如果新建了内核 PDE，会同步到 `kernel_pde_cache[]` 并更新所有用户进程页目录。

### 3.4 Arena 小内存分配器

`sys_malloc()` 对 ≤1024 字节的小内存使用 Arena 机制：

```
┌─────────────────────────────────┐
│  struct arena (元数据头部)       │  ← 一页 4096 字节的开头
│  - desc 指向块描述符            │
│  - large = false                │
│  - cnt = 剩余空闲块数            │
├─────────────────────────────────┤
│  block 0 (如 64 字节)            │
│  block 1                        │
│  ...                            │
│  block N                        │
└─────────────────────────────────┘
```

7 种规格的 `mem_block_desc`：

| 索引 | 块大小 | 每页块数 |
|------|--------|---------|
| 0 | 16 B | 253 |
| 1 | 32 B | 126 |
| 2 | 64 B | 63 |
| 3 | 128 B | 31 |
| 4 | 256 B | 15 |
| 5 | 512 B | 7 |
| 6 | 1024 B | 3 |

大于 1024 字节的分配直接按页分配（`large = true`，`cnt` = 页数）。

---

## 4. 线程与进程

**文件**：`thread/thread.c/h`, `userprog/process.c/h`, `userprog/tss.c/h`

### 4.1 PCB（进程控制块）

每个线程/进程的 PCB 占 **1 页（4096 字节）**，高地址端是内核栈：

```
低地址 ┌──────────────────────┐ ← PCB 页起始 (esp & 0xfffff000)
       │ struct task_struct   │
       │  - self_kstack       │ ← 指向 thread_stack 顶部
       │  - pid, status       │
       │  - priority, name    │
       │  - vruntime, weight  │ ← CFS 调度字段
       │  - fd_table[128]     │ ← 文件描述符表
       │  - pgdir             │ ← 用户进程页目录 (内核线程=NULL)
       │  - cwd_inode_nr      │ ← 当前工作目录
       │  ...                 │
       ├──────────────────────┤
       │ (空闲空间)            │
       ├──────────────────────┤
       │ struct thread_stack  │ ← switch_to 保存 ebp/ebx/edi/esi/eip
       ├──────────────────────┤
       │ struct intr_stack    │ ← 中断/系统调用保存的完整上下文
高地址 └──────────────────────┘ ← PCB 页顶部 (也是 TSS.esp0)
       魔数 0x19870916 用于栈溢出检测
```

### 4.2 线程创建

```c
struct task_struct* thread_start(char* name, int prio, thread_func func, void* arg) {
    struct task_struct* thread = get_kernel_pages(1);  // 分配 1 页作为 PCB
    init_thread(thread, name, prio);                    // 设置基本信息
    thread_create(thread, func, arg);                   // 布局栈帧
    enqueue_task(&cfs_rq, thread);                      // 加入 CFS 就绪队列
    list_append(&thread_all_list, &thread->all_list_tag);
    return thread;
}
```

`thread_create` 在 PCB 页顶部布局栈帧：
- `intr_stack`（中断返回时用）
- `thread_stack`：`eip = kernel_thread`, `function = func`, `func_arg = arg`

首次被调度时，`switch_to` 的 `ret` 会跳转到 `kernel_thread(func, arg)`，开中断后调用 `func(arg)`。

### 4.3 用户进程

`process_execute` 创建用户进程的额外步骤：
1. 分配用户虚拟地址位图（从 `0x8048000` 起）
2. 初始化用户 `mem_block_desc[7]`
3. 创建独立页目录（复制内核 PDE[768..1023]，设置自映射）
4. 线程函数设为 `start_process`

`start_process` 构造 `intr_stack`：
- CS/DS/SS 设为用户态段选择子（DPL=3）
- eflags 设 IF=1, IOPL=0
- esp 指向用户栈 `0xBFFFF000`
- eip 指向用户程序入口
- 通过 `jmp intr_exit` → `iretd` 切换到 Ring 3

### 4.4 TSS

使用**单 TSS**机制：仅在进程切换时更新 `TSS.esp0`，使 CPU 在中断时自动切换到内核栈。

```c
void update_tss_esp(struct task_struct* pthread) {
    tss.esp0 = (uint32_t*)((uint32_t)pthread + PG_SIZE);  // PCB 页顶端
}
```

---

## 5. CFS 调度器

**文件**：`kernel/sched.c/h`, `lib/kernel/rbtree.c/h`

### 5.1 设计思想

CFS（Completely Fair Scheduler）确保每个线程获得「公平」的 CPU 时间：
- 每个线程维护一个虚拟运行时间 `vruntime`
- 调度时选择 `vruntime` 最小的线程（运行最少的线程优先）
- 高优先级线程的 `vruntime` 增长更慢（等价于获得更多实际 CPU 时间）

### 5.2 红黑树

就绪队列使用红黑树 `tasks_timeline`，以 `vruntime` 为 key 排序。`rb_leftmost` 缓存最左节点以实现 O(1) 取最小值。

### 5.3 vruntime 更新

```c
void update_curr(void) {
    uint32_t delta = ticks - curr->exec_start;
    // vruntime += delta * 1024 / weight (定点数避免浮点)
    curr->vruntime += delta * 1024 / curr->weight;
    if (curr->vruntime < cfs_rq.min_vruntime)
        curr->vruntime = cfs_rq.min_vruntime;
    curr->exec_start = ticks;
}
```

### 5.4 权重表

nice 值 -20~19 映射到权重（摘取自 Linux 内核）：

| nice | weight | nice | weight |
|------|--------|------|--------|
| -20 | 88761 | 0 | 1024 |
| -10 | 9548 | 10 | 110 |
| -5 | 3121 | 15 | 36 |
| -1 | 1277 | 19 | 15 |

权重越大，`vruntime` 增长越慢，获得的 CPU 时间越多。

### 5.5 调度流程

```
timer_interrupt_handler()
  ├── update ticks/elapsed_ticks
  └── if (elapsed_ticks >= SCHED_LATENCY_TICKS)
        └── schedule()
              ├── update_curr()           // 更新当前线程 vruntime
              ├── enqueue_task(current)    // 当前线程放回红黑树
              ├── pick_next_task()         // 取最左节点 (vruntime 最小)
              ├── dequeue_task(next)       // 从树中移除
              ├── process_activate(next)   // 切换页表 + TSS.esp0
              └── switch_to(current, next) // 汇编上下文切换
```

---

## 6. 同步机制

**文件**：`thread/sync.c/h`

### 6.1 信号量

```c
struct semaphore {
    uint8_t value;        // 信号量值
    struct list waiters;  // 等待线程链表
};
```

- **P 操作** (`sema_down`)：关中断 → value==0 则 `thread_block` 加入 waiters → value-- → 恢复中断
- **V 操作** (`sema_up`)：关中断 → value++ → 若 waiters 非空则 `thread_unblock` → 恢复中断
- 使用 `while` 循环检查 value ==0 防止虚假唤醒

### 6.2 可重入锁

```c
struct lock {
    struct task_struct* holder;  // 持有者
    struct semaphore semaphore;  // 底层信号量 (value 初始为 1)
    uint32_t holder_repeat_nr;  // 重入计数
};
```

同一线程重复获取锁只增加 `holder_repeat_nr`，释放时递减直到 0 才真正 `sema_up`。

---

## 7. 文件系统

**文件**：`fs/fs.c`, `fs/inode.c`, `fs/dir.c`, `fs/file.c`, `fs/super_block.h`

### 7.1 磁盘布局

```
┌─────┬──────────┬───────────┬───────────┬──────────┬──────────┐
│Boot │ Block    │  Inode    │  Inode    │  Data    │          │
│+SB  │ Bitmap   │  Bitmap   │  Table    │  Blocks  │   ...    │
│     │ (N blks) │  (1 blk)  │ (~86 blks)│          │          │
└─────┴──────────┴───────────┴───────────┴──────────┴──────────┘
Block 0            Block 1+   Block N+1   ...         ...
```

- **超级块**：占 Block 0 的第 1 个扇区（512 字节），魔数 `0x19590321`
- **块位图**：1 bit = 1 block (4KB)，上限 32768 块 = 128MB/分区
- **inode 位图**：1 块 = 32768 bit，只用 4096 个（`MAX_FILES_PER_PART`）
- **inode 表**：每个 inode 约 84 字节，4096 个 inode 占 ~86 块

### 7.2 inode 结构

```c
struct inode {
    uint32_t i_no;
    uint32_t i_size;
    uint32_t i_open_cnts;
    bool write_deny;
    uint32_t i_sectors[15];  // 块索引
    struct list_elem inode_tag;
};
```

**i_sectors[15] 索引方案**：

| 索引 | 类型 | 容量 |
|------|------|------|
| 0~11 | 直接块 | 12 × 4KB = 48KB |
| 12 | 一级间接 | 1024 × 4KB = 4MB |
| 13 | 二级间接 | 1024² × 4KB = 4GB |
| 14 | 三级间接 | 1024³ × 4KB > 4TB |

间接块本身也是 4KB 块，存储 1024 个 `uint32_t` 块号。

### 7.3 目录项

```c
struct dir_entry {
    char filename[MAX_FILE_NAME_LEN];  // 16 字节
    uint32_t i_no;                      // 4 字节
    enum file_types f_type;             // 4 字节
};  // 共 24 字节, 每块存 170 个
```

目录最多使用 `DIR_MAX_BLOCKS = 1036` 个块（12 直接 + 1024 一级间接）。

### 7.4 全局打开文件表

```c
struct file file_table[MAX_FILE_OPEN];  // 512 项
```

每个进程的 `fd_table[128]` 存储全局文件表索引。fd 0/1/2 预留给 stdin/stdout/stderr。

### 7.5 文件系统初始化

`filesys_init` 流程：
1. 遍历所有通道的硬盘分区
2. 读取超级块，检查魔数是否为 `0x19590321`
3. 未格式化 → `partition_format` 创建超级块/位图/根目录
4. 已格式化 → 挂载：读取位图到内存，打开根目录

---

## 8. 系统调用

**文件**：`kernel/syscall.c/h`, `kernel/kernel.asm`, `lib/usr/syscall.c/h`

### 8.1 调用路径

```
用户态                                内核态
───────                              ───────
syscall_wrapper:                     kernel.asm → syscall_handler:
  mov eax, 调用号                       保存上下文 (pushad + 段寄存器)
  mov ebx/ecx/edx, 参数                 切换内核数据段
  int 0x80  ──────────────────────►     push 参数
                                        call [syscall_table + eax*4]
                                        结果写入栈中 eax 位置
  返回值在 eax ◄──────────────────      jmp intr_exit (iretd)
```

### 8.2 用户态封装 (`lib/usr/syscall.c`)

```c
// 0 参数系统调用
#define _syscall0(NUMBER) ({        \
    int retval;                      \
    asm volatile ("int $0x80"        \
        : "=a"(retval)               \
        : "a"(NUMBER)                \
        : "memory");                 \
    retval;                          \
})
```

类似提供 `_syscall1`, `_syscall2`, `_syscall3` 宏。

---

## 9. 设备驱动

### 9.1 键盘 (`device/keyboard.c`)

**中断号**：IRQ1 → 向量号 0x21

**处理流程**：
1. 从端口 `0x60` 读取扫描码
2. 处理 0xE0 前缀（扩展键）
3. 通码(make)：更新修饰键状态（Shift/Ctrl/Alt/CapsLock）
4. 查 `keymap[][2]` 转为 ASCII 字符
5. `ioq_putchar(&kbd_buf, ch)` 放入环形缓冲区
6. `put_char(ch)` 回显到屏幕

### 9.2 定时器 (`device/timer.c`)

**芯片**：8253 PIT, 通道 0, 方波模式

```
频率 = 1193180 / counter_value
counter_value = 1193180 / 100 = 11931
→ 100Hz, 每 10ms 一次中断
```

**时钟中断处理**：
1. 检查 `stack_magic`（栈溢出检测）
2. 累加 `elapsed_ticks`（线程运行 tick 数）
3. 累加全局 `ticks`
4. 若 `elapsed_ticks >= SCHED_LATENCY_TICKS (3)` → 调用 `schedule()`

### 9.3 环形缓冲区 (`device/ioqueue.c`)

**容量**：64 字节

使用信号量实现生产者-消费者模型：
- `has_data` 信号量（初始 0）：消费者（shell 读键盘）等待
- `has_space` 信号量（初始 64）：生产者（键盘中断）等待
- `lock`：保护 `buf[]` / `head` / `tail` 的并发访问

```c
void ioq_putchar(struct ioqueue* ioq, char byte) {
    sema_down(&ioq->has_space);     // P(空位)
    lock_acquire(&ioq->lock);
    ioq->buf[ioq->head] = byte;
    ioq->head = next_pos(ioq->head);
    lock_release(&ioq->lock);
    sema_up(&ioq->has_data);        // V(数据)
}
```

### 9.4 控制台 (`device/console.c`)

包装 `put_char`, `put_str`, `put_int` 等函数，在调用前后加锁 `console_lock`，防止多线程输出交错。

---

## 10. Shell

**文件**：`shell/shell.c/h`

### 10.1 运行方式

Shell 作为**内核线程**运行（`thread_start("shell", 10, my_shell, NULL)`），因为系统未实现 fork/exec。

### 10.2 输入处理

```c
void readline(char* buf, int32_t count) {
    while (读取的字符数 < count) {
        char ch = ioq_getchar(&kbd_buf);  // 阻塞等待键盘输入
        if (ch == '\n') break;
        if (ch == '\b') { 删除一个字符; continue; }
        buf[pos++] = ch;
    }
}
```

### 10.3 命令解析

`cmd_parse(cmd_str, argv, delim)` 按空格分割命令行为 `argv[]` 数组（最多 `MAX_ARG_NR=16` 个参数）。

### 10.4 路径处理

`make_abs_path(path, abs_path)` 将相对路径转绝对路径：
- 以 `/` 开头 → 已是绝对路径
- 否则 → 拼接 `cwd + "/" + path`
- 处理 `.` 和 `..` 目录

### 10.5 提示符

```
[LinkOS /当前目录]$ _
```

---

## 11. 汇编模块详解

### 11.1 `kernel/kernel.asm` — 中断入口表

**核心宏 `VECTOR`**：

```nasm
%macro VECTOR 2
section .text
intr%1entry:
    %2                          ; CPU 不自动压入错误码的中断需要手动 push 0
    push ds / es / fs / gs
    pushad
    ; 发送 EOI
    mov al, 0x20
    out 0xa0, al
    out 0x20, al
    push %1                     ; 中断向量号作为参数
    call [idt_table + %1*4]     ; 调用 C 处理函数
    jmp intr_exit
%endmacro
```

**`syscall_handler`（int 0x80 专用入口）**：
- 保存完整上下文
- 切换到内核数据段
- 从寄存器取 5 个参数：edi → esi → edx → ecx → ebx（反序压栈）
- `call [syscall_table + eax*4]`
- 返回值覆盖栈中保存的 eax → `intr_exit` 后用户态 eax 即为返回值

### 11.2 `kernel/switch.asm` — 上下文切换

```nasm
; switch_to(struct task_struct* prev, struct task_struct* next)
switch_to:
    push esi / edi / ebx / ebp     ; 保存 prev 的寄存器

    mov eax, [esp + 20]            ; eax = prev
    mov [eax], esp                 ; prev->self_kstack = esp (保存栈指针)

    mov eax, [esp + 24]            ; eax = next
    mov esp, [eax]                 ; esp = next->self_kstack (恢复栈指针)

    pop ebp / ebx / edi / esi      ; 恢复 next 的寄存器
    ret                            ; pop eip → 跳转到 next 线程
```

- `self_kstack` 是 `struct task_struct` 的**第一个成员**，偏移为 0
- 首次调度的线程：`ret` 会弹出 `kernel_thread` 地址

### 11.3 `lib/kernel/print.asm` — VGA 输出

直接操作 VGA 文本模式显存（段选择子 `gs = 0x18`，对应物理地址 `0xB8000`）。

**光标操作**：通过 CRT 控制器端口 `0x3D4/0x3D5` 读写光标位置（寄存器 0x0E 高 8 位，0x0F 低 8 位）。

**关键函数**：

| 函数 | 说明 |
|------|------|
| `put_char` | 处理 `\n`(换行+回车)、`\b`(退格+空格覆盖)、`\t`(对齐到 8 列)，普通字符写入 `gs:[bx*2]`，自动滚屏 |
| `put_str` | 循环调用 `put_char` 直到遇到 `\0` |
| `put_int` | 将 32 位整数转十进制字符串后输出 |
| `put_hex` | 输出 `0x` 前缀 + 8 位十六进制 |
| `cls_screen` | 用空格(0x0720) 填充 80×25 显存，重置光标到 0 |
| `set_cursor` | 设置光标到指定位置 |

**滚屏实现**：将第 1~24 行内容拷贝到第 0~23 行（`rep movsd`），最后一行填充空格。

---

## 附录：关键数据结构大小

| 结构体 | 大小 | 说明 |
|--------|------|------|
| `struct task_struct` | 4096 B (1 页) | PCB，含内核栈 |
| `struct inode` | ~84 B | 磁盘 inode |
| `struct dir_entry` | 24 B | 目录项 |
| `struct super_block` | 512 B | 超级块 |
| `struct file` | ~12 B | 打开文件表项 |
| `struct gdt_desc` | 8 B | GDT 描述符 |
| `struct gate_desc` | 8 B | IDT 中断门描述符 |
| TSS | 104 B | 任务状态段 |
