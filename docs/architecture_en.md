# LinkOS Architecture & Code Reference

[中文版](architecture.md)

This document provides a detailed explanation of each LinkOS subsystem — design principles, core data structures, and key implementation details.

---

## Table of Contents

1. [Boot Process](#1-boot-process)
2. [Interrupt System](#2-interrupt-system)
3. [Memory Management](#3-memory-management)
4. [Threads & Processes](#4-threads--processes)
5. [CFS Scheduler](#5-cfs-scheduler)
6. [Synchronization](#6-synchronization)
7. [Filesystem](#7-filesystem)
8. [System Calls](#8-system-calls)
9. [Device Drivers](#9-device-drivers)
10. [Shell](#10-shell)
11. [Assembly Modules](#11-assembly-modules)

---

## 1. Boot Process

### 1.1 MBR (`mbr.asm`)

The BIOS loads sector 0 (512 bytes) to physical address `0x7C00` and jumps to it.

**Workflow**:
1. Initialize segment registers `DS=ES=SS=0`, `SP=0x7C00`
2. Clear screen via `INT 0x10`
3. Call `rd_disk_m_16` to read 4 sectors starting from sector 2 to `0x900` (Loader)
4. `jmp LOADER_BASE_ADDR` to hand off to Loader

**Disk read (Real Mode PIO)**:
```
Set sector count → write LBA to ports 0x1F3–0x1F6 → write read cmd 0x20 to 0x1F7
→ poll 0x1F7 until DRQ=1 → insw from 0x1F0
```

The last two bytes must be `0x55AA` (MBR signature), otherwise the BIOS won't recognize it.

### 1.2 Loader (`loader.asm`)

The Loader is the most complex part of the boot process (~656 lines), transitioning from 16-bit real mode to 32-bit protected mode with paging.

**Phase 1 — Memory Detection (Real Mode)**:
- First try INT 0x15 EAX=0xE820 (most detailed, returns ARDS array)
- Fallback to INT 0x15 AX=0xE801 (up to 4GB)
- Last resort INT 0x15 AH=0x88 (up to 64MB)
- Result stored at `0x0B08` (`total_mem_bytes`)

**Phase 2 — Enter Protected Mode**:
1. Enable A20 gate (write to port `0x92`, set bit 1)
2. `lgdt` to load the GDT (base address `0x908`, 3 segment descriptors)
3. Set `CR0.PE = 1` → far jump `jmp dword SELECTOR_CODE:p_mode_start`

**Phase 3 — Enable Paging**:
1. Create Page Directory (PD) at `0x100000` (1MB)
2. PD[0] and PD[768] both point to the same page table (identity-map low 1MB + map to `0xC0000000`)
3. PD[1023] points to PD itself (**page table self-mapping** for accessing page tables via virtual addresses)
4. Set `CR3 = 0x100000`, set `CR0.PG = 1`

**Phase 4 — Load Kernel**:
1. `rd_disk_m_32` reads 200 sectors from sector 9 to physical address `0x70000`
2. `kernel_init` parses ELF format: iterates Program Header Table, copies each `PT_LOAD` segment's `p_filesz` bytes from `p_offset` to `p_vaddr`
3. Far jump to `KERNEL_ENTRY_POINT (0xc0001500)`

### 1.3 Key Address Constants (`include/boot.inc`)

| Constant | Value | Description |
|----------|-------|-------------|
| `LOADER_BASE_ADDR` | `0x900` | Loader load address |
| `LOADER_START_SECTOR` | `0x2` | Loader disk start sector |
| `KERNEL_START_SECTOR` | `0x9` | Kernel disk start sector |
| `KERNEL_BIN_BASE_ADDR` | `0x70000` | Kernel ELF temporary load address |
| `KERNEL_ENTRY_POINT` | `0xc0001500` | Kernel entry virtual address |
| `PAGE_DIR_TABLE_POS` | `0x100000` | Page directory physical address |

---

## 2. Interrupt System

**Files**: `kernel/interrupt.c`, `kernel/kernel.asm`

### 2.1 IDT Initialization

- 129 interrupt descriptors total (`IDT_DESC_CNT = 0x81`), covering vectors 0x00–0x80
- 0x00–0x1F: CPU exceptions (divide-by-zero, Page Fault, Invalid Opcode, etc.)
- 0x20–0x2F: External hardware interrupts (remapped via 8259A PIC)
- 0x80: Syscall entry (**DPL=3**, accessible from user mode)

### 2.2 8259A PIC Programming

```
Master ICW1–ICW4 → ports 0x20/0x21
Slave  ICW1–ICW4 → ports 0xA0/0xA1
Master IRQ base vector = 0x20 (timer=0x20, keyboard=0x21, cascade=0x22)
Slave  IRQ base vector = 0x28 (hard disk=0x2E)
OCW1 enables: IRQ0(timer), IRQ1(keyboard), IRQ2(cascade), IRQ14(hard disk)
```

### 2.3 Interrupt Entry (`kernel.asm`)

The `VECTOR` macro generates entry code for each interrupt vector:

```asm
; 1. Save context
push ds / es / fs / gs
pushad

; 2. Send EOI (for external interrupts)
mov al, 0x20
out 0xa0, al    ; slave EOI
out 0x20, al    ; master EOI

; 3. Call C handler
push vec_no
call [idt_table + vec_no*4]

; 4. Restore context
jmp intr_exit   ; popad + pop segments + iretd
```

### 2.4 Exception Handling

- Generic exception handler `general_intr_handler`: prints vector number then halts with `while(1)`
- Page Fault has a dedicated handler that additionally prints CR2 (the faulting virtual address)

---

## 3. Memory Management

**Files**: `kernel/memory.c/h`

### 3.1 Physical Memory Pools

Total system memory is read from `0x0B08` (stored during Loader phase). After subtracting the low 1MB, the remaining physical memory is split evenly into two pools:

| Pool | Purpose |
|------|---------|
| `kernel_pool` | Kernel page allocation |
| `user_pool` | User process page allocation |

Each pool is managed by a **bitmap**: 1 bit = 1 page (4KB). Bitmaps are stored at virtual address `0xc009a000`.

### 3.2 Virtual Address Management

- Kernel virtual address pool starts at: `K_HEAP_START = 0xc0100000`
- Each user process has its own virtual address pool (bitmap in `userprog_vaddr`)
- User process start virtual address: `USER_VADDR_START = 0x8048000` (consistent with Linux)

### 3.3 Page Table Operations

Using the **self-mapping** mechanism (PD[1023] → PD itself), any PDE/PTE can be accessed via virtual addresses:

```c
// Get the virtual address of the PTE for vaddr
uint32_t* pte_ptr(uint32_t vaddr) {
    return (uint32_t*)(0xffc00000 + ((vaddr & 0xffc00000) >> 10) + PTE_IDX(vaddr) * 4);
}

// Get the virtual address of the PDE for vaddr
uint32_t* pde_ptr(uint32_t vaddr) {
    return (uint32_t*)(0xfffff000 + PDE_IDX(vaddr) * 4);
}
```

When **page_table_add** creates a new kernel PDE, it syncs to `kernel_pde_cache[]` and updates all user process page directories.

### 3.4 Arena Small-Object Allocator

`sys_malloc()` uses the Arena mechanism for allocations ≤ 1024 bytes:

```
┌─────────────────────────────────┐
│  struct arena (metadata header)  │  ← start of a 4096-byte page
│  - desc → block descriptor      │
│  - large = false                 │
│  - cnt = remaining free blocks   │
├─────────────────────────────────┤
│  block 0 (e.g. 64 bytes)        │
│  block 1                        │
│  ...                            │
│  block N                        │
└─────────────────────────────────┘
```

7 size classes of `mem_block_desc`:

| Index | Block Size | Blocks per Page |
|-------|-----------|-----------------|
| 0 | 16 B | 253 |
| 1 | 32 B | 126 |
| 2 | 64 B | 63 |
| 3 | 128 B | 31 |
| 4 | 256 B | 15 |
| 5 | 512 B | 7 |
| 6 | 1024 B | 3 |

Allocations larger than 1024 bytes are fulfilled by whole-page allocation (`large = true`, `cnt` = page count).

---

## 4. Threads & Processes

**Files**: `thread/thread.c/h`, `userprog/process.c/h`, `userprog/tss.c/h`

### 4.1 PCB (Process Control Block)

Each thread/process PCB occupies **1 page (4096 bytes)**, with the kernel stack at the high end:

```
Low addr  ┌──────────────────────┐ ← PCB page start (esp & 0xfffff000)
          │ struct task_struct   │
          │  - self_kstack       │ ← points to top of thread_stack
          │  - pid, status       │
          │  - priority, name    │
          │  - vruntime, weight  │ ← CFS scheduling fields
          │  - fd_table[128]     │ ← file descriptor table
          │  - pgdir             │ ← user process page dir (NULL for kernel threads)
          │  - cwd_inode_nr      │ ← current working directory
          │  ...                 │
          ├──────────────────────┤
          │ (free space)         │
          ├──────────────────────┤
          │ struct thread_stack  │ ← switch_to saves ebp/ebx/edi/esi/eip
          ├──────────────────────┤
          │ struct intr_stack    │ ← full context saved by interrupt/syscall
High addr └──────────────────────┘ ← PCB page top (also TSS.esp0)
          Magic number 0x19870916 for stack overflow detection
```

### 4.2 Thread Creation

```c
struct task_struct* thread_start(char* name, int prio, thread_func func, void* arg) {
    struct task_struct* thread = get_kernel_pages(1);  // allocate 1 page for PCB
    init_thread(thread, name, prio);                    // set basic info
    thread_create(thread, func, arg);                   // lay out stack frame
    enqueue_task(&cfs_rq, thread);                      // add to CFS ready queue
    list_append(&thread_all_list, &thread->all_list_tag);
    return thread;
}
```

`thread_create` lays out the stack frame at the top of the PCB page:
- `intr_stack` (used for interrupt return)
- `thread_stack`: `eip = kernel_thread`, `function = func`, `func_arg = arg`

On first schedule, `switch_to`'s `ret` jumps to `kernel_thread(func, arg)`, which enables interrupts then calls `func(arg)`.

### 4.3 User Processes

`process_execute` additional steps for creating user processes:
1. Allocate user virtual address bitmap (starting from `0x8048000`)
2. Initialize user `mem_block_desc[7]`
3. Create independent page directory (copy kernel PDE[768..1023], set up self-mapping)
4. Thread function set to `start_process`

`start_process` constructs `intr_stack`:
- CS/DS/SS set to user-mode segment selectors (DPL=3)
- eflags set IF=1, IOPL=0
- esp points to user stack `0xBFFFF000`
- eip points to user program entry
- Switches to Ring 3 via `jmp intr_exit` → `iretd`

### 4.4 TSS

Uses **single-TSS** mechanism: only updates `TSS.esp0` on process switch, so the CPU automatically switches to the kernel stack on interrupt.

```c
void update_tss_esp(struct task_struct* pthread) {
    tss.esp0 = (uint32_t*)((uint32_t)pthread + PG_SIZE);  // top of PCB page
}
```

---

## 5. CFS Scheduler

**Files**: `kernel/sched.c/h`, `lib/kernel/rbtree.c/h`

### 5.1 Design Philosophy

CFS (Completely Fair Scheduler) ensures each thread gets a "fair" share of CPU time:
- Each thread maintains a virtual runtime `vruntime`
- The scheduler always picks the thread with the smallest `vruntime` (least-served thread first)
- Higher-priority threads' `vruntime` grows slower (equivalent to getting more actual CPU time)

### 5.2 Red-Black Tree

The ready queue uses a red-black tree `tasks_timeline`, keyed by `vruntime`. `rb_leftmost` caches the leftmost node for O(1) minimum retrieval.

### 5.3 vruntime Update

```c
void update_curr(void) {
    uint32_t delta = ticks - curr->exec_start;
    // vruntime += delta * 1024 / weight (fixed-point to avoid floats)
    curr->vruntime += delta * 1024 / curr->weight;
    if (curr->vruntime < cfs_rq.min_vruntime)
        curr->vruntime = cfs_rq.min_vruntime;
    curr->exec_start = ticks;
}
```

### 5.4 Weight Table

Nice values -20 to 19 map to weights (taken from the Linux kernel):

| nice | weight | nice | weight |
|------|--------|------|--------|
| -20 | 88761 | 0 | 1024 |
| -10 | 9548 | 10 | 110 |
| -5 | 3121 | 15 | 36 |
| -1 | 1277 | 19 | 15 |

Higher weight → slower `vruntime` growth → more CPU time.

### 5.5 Scheduling Flow

```
timer_interrupt_handler()
  ├── update ticks/elapsed_ticks
  └── if (elapsed_ticks >= SCHED_LATENCY_TICKS)
        └── schedule()
              ├── update_curr()           // update current thread's vruntime
              ├── enqueue_task(current)    // put current thread back in RB-tree
              ├── pick_next_task()         // get leftmost node (smallest vruntime)
              ├── dequeue_task(next)       // remove from tree
              ├── process_activate(next)   // switch page table + TSS.esp0
              └── switch_to(current, next) // assembly context switch
```

---

## 6. Synchronization

**Files**: `thread/sync.c/h`

### 6.1 Semaphores

```c
struct semaphore {
    uint8_t value;        // semaphore value
    struct list waiters;  // waiting thread list
};
```

- **P operation** (`sema_down`): disable interrupts → if value==0 then `thread_block` and add to waiters → value-- → restore interrupts
- **V operation** (`sema_up`): disable interrupts → value++ → if waiters non-empty then `thread_unblock` → restore interrupts
- Uses `while` loop to check value==0 to prevent spurious wakeups

### 6.2 Reentrant Locks

```c
struct lock {
    struct task_struct* holder;  // lock holder
    struct semaphore semaphore;  // underlying semaphore (value init to 1)
    uint32_t holder_repeat_nr;  // reentry count
};
```

If the same thread acquires the lock again, only `holder_repeat_nr` is incremented. On release, it decrements until reaching 0 before actually calling `sema_up`.

---

## 7. Filesystem

**Files**: `fs/fs.c`, `fs/inode.c`, `fs/dir.c`, `fs/file.c`, `fs/super_block.h`

### 7.1 Disk Layout

```
┌─────┬──────────┬───────────┬───────────┬──────────┬──────────┐
│Boot │ Block    │  Inode    │  Inode    │  Data    │          │
│+SB  │ Bitmap   │  Bitmap   │  Table    │  Blocks  │   ...    │
│     │ (N blks) │  (1 blk)  │ (~86 blks)│          │          │
└─────┴──────────┴───────────┴───────────┴──────────┴──────────┘
Block 0            Block 1+   Block N+1   ...         ...
```

- **Superblock**: occupies the 1st sector of Block 0 (512 bytes), magic number `0x19590321`
- **Block bitmap**: 1 bit = 1 block (4KB), up to 32768 blocks = 128MB per partition
- **Inode bitmap**: 1 block = 32768 bits, only 4096 used (`MAX_FILES_PER_PART`)
- **Inode table**: each inode ~84 bytes, 4096 inodes occupy ~86 blocks

### 7.2 Inode Structure

```c
struct inode {
    uint32_t i_no;
    uint32_t i_size;
    uint32_t i_open_cnts;
    bool write_deny;
    uint32_t i_sectors[15];  // block indices
    struct list_elem inode_tag;
};
```

**i_sectors[15] indexing scheme**:

| Index | Type | Capacity |
|-------|------|----------|
| 0–11 | Direct blocks | 12 × 4KB = 48KB |
| 12 | Single indirect | 1024 × 4KB = 4MB |
| 13 | Double indirect | 1024² × 4KB = 4GB |
| 14 | Triple indirect | 1024³ × 4KB > 4TB |

Indirect blocks are themselves 4KB blocks storing 1024 `uint32_t` block numbers.

### 7.3 Directory Entries

```c
struct dir_entry {
    char filename[MAX_FILE_NAME_LEN];  // 16 bytes
    uint32_t i_no;                      // 4 bytes
    enum file_types f_type;             // 4 bytes
};  // 24 bytes total, 170 per block
```

Directories can use up to `DIR_MAX_BLOCKS = 1036` blocks (12 direct + 1024 single indirect).

### 7.4 Global Open File Table

```c
struct file file_table[MAX_FILE_OPEN];  // 512 entries
```

Each process's `fd_table[128]` stores indices into the global file table. fd 0/1/2 are reserved for stdin/stdout/stderr.

### 7.5 Filesystem Initialization

`filesys_init` flow:
1. Iterate all hard disk partitions across all channels
2. Read superblock, check if magic number is `0x19590321`
3. Unformatted → `partition_format` creates superblock/bitmaps/root directory
4. Already formatted → mount: read bitmaps into memory, open root directory

---

## 8. System Calls

**Files**: `kernel/syscall.c/h`, `kernel/kernel.asm`, `lib/usr/syscall.c/h`

### 8.1 Call Path

```
User mode                            Kernel mode
─────────                            ───────────
syscall_wrapper:                     kernel.asm → syscall_handler:
  mov eax, syscall_number               save context (pushad + segments)
  mov ebx/ecx/edx, arguments            switch to kernel data segment
  int 0x80  ──────────────────────►     push arguments
                                        call [syscall_table + eax*4]
                                        write return value to saved eax on stack
  return value in eax ◄──────────────  jmp intr_exit (iretd)
```

### 8.2 User-Space Wrappers (`lib/usr/syscall.c`)

```c
// 0-argument syscall
#define _syscall0(NUMBER) ({        \
    int retval;                      \
    asm volatile ("int $0x80"        \
        : "=a"(retval)               \
        : "a"(NUMBER)                \
        : "memory");                 \
    retval;                          \
})
```

Similarly provides `_syscall1`, `_syscall2`, `_syscall3` macros.

---

## 9. Device Drivers

### 9.1 Keyboard (`device/keyboard.c`)

**IRQ**: IRQ1 → vector 0x21

**Processing flow**:
1. Read scancode from port `0x60`
2. Handle 0xE0 prefix (extended keys)
3. Make code: update modifier key state (Shift/Ctrl/Alt/CapsLock)
4. Look up `keymap[][2]` to convert to ASCII character
5. `ioq_putchar(&kbd_buf, ch)` — put into ring buffer
6. `put_char(ch)` — echo to screen

### 9.2 Timer (`device/timer.c`)

**Chip**: 8253 PIT, channel 0, square wave mode

```
Frequency = 1193180 / counter_value
counter_value = 1193180 / 100 = 11931
→ 100Hz, one interrupt every 10ms
```

**Timer interrupt handler**:
1. Check `stack_magic` (stack overflow detection)
2. Increment `elapsed_ticks` (thread runtime in ticks)
3. Increment global `ticks`
4. If `elapsed_ticks >= SCHED_LATENCY_TICKS (3)` → call `schedule()`

### 9.3 Ring Buffer (`device/ioqueue.c`)

**Capacity**: 64 bytes

Uses semaphores for producer-consumer model:
- `has_data` semaphore (initial 0): consumer (shell reading keyboard) waits
- `has_space` semaphore (initial 64): producer (keyboard interrupt) waits
- `lock`: protects concurrent access to `buf[]` / `head` / `tail`

```c
void ioq_putchar(struct ioqueue* ioq, char byte) {
    sema_down(&ioq->has_space);     // P(space)
    lock_acquire(&ioq->lock);
    ioq->buf[ioq->head] = byte;
    ioq->head = next_pos(ioq->head);
    lock_release(&ioq->lock);
    sema_up(&ioq->has_data);        // V(data)
}
```

### 9.4 Console (`device/console.c`)

Wraps `put_char`, `put_str`, `put_int`, etc. with a `console_lock` before and after calls to prevent interleaved output from multiple threads.

---

## 10. Shell

**Files**: `shell/shell.c/h`

### 10.1 Execution Model

The shell runs as a **kernel thread** (`thread_start("shell", 10, my_shell, NULL)`) because the system does not implement fork/exec.

### 10.2 Input Handling

```c
void readline(char* buf, int32_t count) {
    while (characters_read < count) {
        char ch = ioq_getchar(&kbd_buf);  // blocks waiting for keyboard input
        if (ch == '\n') break;
        if (ch == '\b') { delete one char; continue; }
        buf[pos++] = ch;
    }
}
```

### 10.3 Command Parsing

`cmd_parse(cmd_str, argv, delim)` splits the command line by spaces into an `argv[]` array (up to `MAX_ARG_NR=16` arguments).

### 10.4 Path Handling

`make_abs_path(path, abs_path)` converts relative paths to absolute:
- Starts with `/` → already absolute
- Otherwise → concatenate `cwd + "/" + path`
- Handles `.` and `..` directories

### 10.5 Prompt Format

```
[LinkOS /current_directory]$ _
```

---

## 11. Assembly Modules

### 11.1 `kernel/kernel.asm` — Interrupt Entry Table

**Core macro `VECTOR`**:

```nasm
%macro VECTOR 2
section .text
intr%1entry:
    %2                          ; push 0 for interrupts without CPU error code
    push ds / es / fs / gs
    pushad
    ; send EOI
    mov al, 0x20
    out 0xa0, al
    out 0x20, al
    push %1                     ; vector number as argument
    call [idt_table + %1*4]     ; call C handler
    jmp intr_exit
%endmacro
```

**`syscall_handler` (int 0x80 dedicated entry)**:
- Save full context
- Switch to kernel data segment
- Extract 5 arguments from registers: edi → esi → edx → ecx → ebx (pushed in reverse)
- `call [syscall_table + eax*4]`
- Return value overwrites saved eax on stack → after `intr_exit`, user-mode eax contains the return value

### 11.2 `kernel/switch.asm` — Context Switch

```nasm
; switch_to(struct task_struct* prev, struct task_struct* next)
switch_to:
    push esi / edi / ebx / ebp     ; save prev's registers

    mov eax, [esp + 20]            ; eax = prev
    mov [eax], esp                 ; prev->self_kstack = esp (save stack pointer)

    mov eax, [esp + 24]            ; eax = next
    mov esp, [eax]                 ; esp = next->self_kstack (restore stack pointer)

    pop ebp / ebx / edi / esi      ; restore next's registers
    ret                            ; pop eip → jump to next thread
```

- `self_kstack` is the **first member** of `struct task_struct`, at offset 0
- For a newly scheduled thread: `ret` pops the `kernel_thread` address

### 11.3 `lib/kernel/print.asm` — VGA Output

Directly operates VGA text mode video memory (segment selector `gs = 0x18`, maps to physical `0xB8000`).

**Cursor control**: reads/writes cursor position via CRT controller ports `0x3D4/0x3D5` (register 0x0E = high 8 bits, 0x0F = low 8 bits).

**Key functions**:

| Function | Description |
|----------|-------------|
| `put_char` | Handles `\n` (newline+CR), `\b` (backspace+overwrite with space), `\t` (align to 8 columns), normal chars write to `gs:[bx*2]`, auto-scroll |
| `put_str` | Loops calling `put_char` until `\0` |
| `put_int` | Converts 32-bit integer to decimal string then outputs |
| `put_hex` | Outputs `0x` prefix + 8-digit hexadecimal |
| `cls_screen` | Fill 80×25 video memory with spaces (0x0720), reset cursor to 0 |
| `set_cursor` | Set cursor to specified position |

**Scrolling**: copies rows 1–24 to rows 0–23 (`rep movsd`), fills last row with spaces.

---

## Appendix: Key Data Structure Sizes

| Structure | Size | Description |
|-----------|------|-------------|
| `struct task_struct` | 4096 B (1 page) | PCB, includes kernel stack |
| `struct inode` | ~84 B | On-disk inode |
| `struct dir_entry` | 24 B | Directory entry |
| `struct super_block` | 512 B | Superblock |
| `struct file` | ~12 B | Open file table entry |
| `struct gdt_desc` | 8 B | GDT descriptor |
| `struct gate_desc` | 8 B | IDT interrupt gate descriptor |
| TSS | 104 B | Task State Segment |
