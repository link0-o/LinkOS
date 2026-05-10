# LinkOS

[中文文档](README.md)

A 32-bit x86 operating system written from scratch, inspired by the book *erta OS: Truth Restored* and others. Runs on the Bochs emulator.

## Features

- **Full Boot Chain**: MBR → Loader (Real Mode → Protected Mode → Paging → ELF Loading)
- **Memory Management**: Physical/virtual bitmaps, two-level page tables, self-mapping, Arena small-object allocator (7 size classes: 16–1024B)
- **Interrupt System**: 8259A PIC, 129-entry IDT, `int 0x80` syscall gate
- **CFS Scheduler**: Red-black tree based Completely Fair Scheduler with Linux-standard weight table
- **Threads & Processes**: Kernel threads + Ring 3 user processes, single-TSS esp0 switching
- **Synchronization**: Semaphores + reentrant mutexes
- **ext2-like Filesystem**: Superblock + bitmaps + inode with triple indirect indexing, 4KB blocks, up to 4096 files
- **Interactive Shell**: 12 built-in commands (ls, cd, cat, echo, mkdir, rm, etc.) with `>` redirection
- **Device Drivers**: Keyboard (scancode translation + ring buffer), 8253 PIT timer (100Hz), PCI IDE Bus Master DMA disk, VGA text mode
- **DMA Subsystem**: Includes both the 8237A ISA DMA controller abstraction and a PCI IDE Bus Master DMA disk path

## Project Structure

```
LinkOS/
├── bochs/                   # Bochs emulator runtime environment
│   ├── bochsrc.txt          # Bochs configuration
│   └── disk/                # Disk image directory
│       ├── hd60M.img        # Primary disk (system: MBR/Loader/Kernel)
│       └── hd80M.img        # Secondary disk (filesystem partition)
│
└── code/                    # All source code (~9400 lines, 66 files)
    ├── mbr.asm              # Master Boot Record (512 bytes, loads Loader)
    ├── loader.asm           # Loader (real→protected mode→paging→kernel load)
    ├── include/boot.inc     # Boot-stage shared constants (addresses, GDT attrs)
    ├── makefile             # Build system
    │
    ├── kernel/              # Kernel core
    │   ├── main.c           # Kernel entry point main()
    │   ├── init.c/h         # Subsystem initialization orchestration
    │   ├── global.h         # GDT selectors, global macros
    │   ├── interrupt.c/h    # IDT + 8259A PIC + interrupt control
    │   ├── kernel.asm       # Interrupt entry table + syscall entry + intr_exit
    │   ├── memory.c/h       # Physical/virtual memory management + page tables + Arena
    │   ├── sched.c/h        # CFS scheduler (red-black tree)
    │   ├── switch.asm       # Thread context switch (save/restore esp)
    │   └── syscall.c/h      # Syscall dispatch table + sys_malloc/sys_free
    │
    ├── thread/              # Threading subsystem
    │   ├── thread.c/h       # PCB/TCB + thread create/block/unblock + idle thread
    │   └── sync.c/h         # Semaphores + reentrant locks
    │
    ├── userprog/            # User processes
    │   ├── process.c/h      # User process creation + page directory + Ring 3 switch
    │   └── tss.c/h          # TSS management (esp0 update)
    │
    ├── device/              # Device drivers
    │   ├── keyboard.c/h     # Keyboard interrupt handler + scancode mapping
    │   ├── timer.c/h        # 8253 PIT initialization + timer interrupt (100Hz)
    │   ├── console.c/h      # Console output lock
    │   ├── dma.c/h          # 8237A DMA controller driver + channel programming API
    │   ├── ide.c/h          # IDE driver (PCI BMIDE DMA first, PIO fallback)
    │   └── ioqueue.c/h      # Producer-consumer ring buffer
    │
    ├── fs/                  # Filesystem
    │   ├── fs.c/h           # FS init/mount + 16 sys_* syscall implementations
    │   ├── super_block.h    # Superblock structure (512 bytes)
    │   ├── inode.c/h        # Inode operations + triple indirect indexing
    │   ├── dir.c/h          # Directory operations (search/create/delete entries)
    │   └── file.c/h         # File operations (global open file table + read/write)
    │
    ├── shell/               # Interactive shell
    │   └── shell.c/h        # 12 built-in commands + command parsing + path handling
    │
    └── lib/                 # Library functions
        ├── string.c/h       # memset/memcpy/strcmp/strcpy etc.
        ├── stdint.h         # Integer type definitions
        ├── stdarg.h         # Variadic argument macros
        ├── kernel/          # Kernel-space library
        │   ├── print.asm/h  # VGA text output (put_char/put_str/put_int/cls_screen)
        │   ├── bitmap.c/h   # Bitmap operations
        │   ├── list.c/h     # Doubly linked list
        │   ├── rbtree.c/h   # Red-black tree (used by CFS scheduler)
        │   ├── debug.c/h    # ASSERT + PANIC macros
        │   └── io.h         # Port I/O inline assembly (inb/outb/insw/outsw)
        └── usr/             # User-space library
            ├── stdio.c/h    # printf/sprintf
            └── syscall.c/h  # User-space syscall wrappers
```

## Boot Sequence

```
BIOS (0x7C00)
  │
  ▼
MBR (Sector 0) ─── Read Loader from disk to 0x900
  │
  ▼
Loader (Sectors 2–5)
  ├── Detect physical memory (E820/E801/0x88)
  ├── Set up GDT, enable A20
  ├── Set CR0.PE → enter Protected Mode
  ├── Initialize page directory/tables → enable Paging
  ├── Read kernel.bin from disk to 0x70000
  ├── Parse ELF, copy segments to 0xc0001500
  └── Jump to KERNEL_ENTRY_POINT
        │
        ▼
Kernel main() (0xc0001500)
  ├── init_all()  ← initialize all subsystems
  │   ├── idt_init()       IDT + 8259A
  │   ├── mem_init()       memory pools + Arena
  │   ├── thread_init()    PCB lists + CFS + idle thread
  │   ├── timer_init()     PIT 100Hz
  │   ├── console_init()   console lock
  │   ├── keyboard_init()  keyboard interrupt
  │   ├── tss_init()       TSS + user segment descriptors
  │   └── filesys_init()   filesystem mount
  ├── cls_screen()
  ├── thread_start("shell") ← start Shell as kernel thread
  └── intr_enable() → idle loop
```

## Disk Layout

### Primary Disk (hd60M.img)

| Sector | Content |
|--------|---------|
| 0 | MBR (512 bytes, ends with `0x55AA`) |
| 2–5 | Loader (4 sectors) |
| 9–208 | Kernel (200 sectors, max 100KB) |

### Secondary Partition (hd80M.img) — Filesystem Layout

| Block | Content |
|-------|---------|
| 0 | Boot sector + Superblock (512B) |
| 1–N | Block bitmap |
| N+1 | Inode bitmap (1 block, 4096 inodes) |
| ... | Inode table (~86 blocks) |
| ... | Data blocks |

- **Block size**: 4096 bytes (8 sectors)
- **Magic number**: `0x19590321`
- **Inode indexing**: 12 direct + 1 single indirect + 1 double indirect + 1 triple indirect

## Memory Map

| Address Range | Purpose |
|---------------|---------|
| `0x00000000 – 0x000003FF` | Interrupt Vector Table (IVT) |
| `0x00000900` | Loader load address |
| `0x00007C00` | MBR load address |
| `0x00070000` | Kernel temporary load address |
| `0x00100000` | Page Directory Table (at 1MB) |
| `0xc0000000 –` | Kernel virtual address space |
| `0xc0001500` | Kernel entry point (virtual) |
| `0xc0100000` | Kernel heap start |
| `0xc009a000` | Bitmap management area |

## Shell Commands

| Command | Description | Example |
|---------|-------------|---------|
| `help` | Show help | `help` |
| `clear` / `cls` | Clear screen | `clear` |
| `pwd` | Print working directory | `pwd` |
| `cd <path>` | Change directory | `cd /home` |
| `ls [path]` | List directory contents | `ls /` |
| `mkdir <path>` | Create directory | `mkdir /test` |
| `rmdir <path>` | Remove empty directory | `rmdir /test` |
| `touch <file>` | Create empty file | `touch /hello.txt` |
| `rm <file>` | Remove file | `rm /hello.txt` |
| `cat <file>` | Display file contents | `cat /readme` |
| `echo <text> [> file]` | Print text / write to file | `echo hello > /greet.txt` |
| `stat <path>` | Show file/directory info | `stat /home` |

## System Calls

Triggered via `int 0x80`. Syscall number in eax, arguments in ebx/ecx/edx:

| Number | Name | Description |
|--------|------|-------------|
| 0 | `getpid` | Get current process PID |
| 1 | `write` | Write to file / console |
| 2 | `malloc` | Allocate heap memory |
| 3 | `free` | Free heap memory |
| 4 | `open` | Open/create file |
| 5 | `close` | Close file descriptor |
| 6 | `read` | Read file / keyboard input |
| 7 | `lseek` | Seek file offset |
| 8 | `unlink` | Delete file |
| 9 | `mkdir` | Create directory |
| 10 | `opendir` | Open directory |
| 11 | `closedir` | Close directory |
| 12 | `readdir` | Read directory entry |
| 13 | `rewinddir` | Reset directory read position |
| 14 | `rmdir` | Remove directory |
| 15 | `getcwd` | Get current working directory |
| 16 | `chdir` | Change working directory |
| 17 | `stat` | Get file information |

## Building & Running

### Prerequisites

- **GCC** (with `-m32` support)
- **NASM** (assembler)
- **LD** (GNU linker, `elf_i386` support)
- **Bochs** (x86 emulator)
- 32-bit C library: `sudo apt install gcc-multilib`

### Build

```bash
cd code
make clean && make
```

`make` automatically:
1. Compiles all `.c`/`.asm` source files to `.o`
2. Links into `kernel.bin` (entry at `0xc0001500`)
3. Assembles `mbr.bin` and `loader.bin`
4. Writes all three to `../bochs/disk/hd60M.img` via `dd`

### Run

```bash
cd ../bochs
./bin/bochs -f bochsrc.txt -q
```

### Debugging

Bochs debugger commands:

```
bp 0x7c00              # Breakpoint at MBR entry
bp 0xc0001500          # Breakpoint at kernel entry
c                      # Continue
s                      # Single step
n                      # Step over call
r                      # View registers
x /20xb 0x70000        # Examine memory
u /10 0xc0001500       # Disassemble
```

## Technical Details

### Compiler Flags

| Flag | Purpose |
|------|---------|
| `-m32` | Generate 32-bit code |
| `-fno-builtin` | Disable GCC built-in function optimizations |
| `-mno-sse -mno-sse2` | Disable SSE instructions (OSFXSR not enabled in CR4) |
| `-Wall -W` | Enable all warnings |
| `-Ttext 0xc0001500` | Kernel entry address |
| `-e main` | Specify entry symbol |

### GDT Layout

| Index | Selector | Description |
|-------|----------|-------------|
| 0 | `0x00` | Null descriptor |
| 1 | `0x08` | Kernel code segment (DPL=0) |
| 2 | `0x10` | Kernel data segment (DPL=0) |
| 3 | `0x18` | Video memory segment (DPL=0) |
| 4 | `0x20` | TSS (DPL=0) |
| 5 | `0x2B` | User code segment (DPL=3) |
| 6 | `0x33` | User data segment (DPL=3) |

### Scheduler

Uses the Linux CFS (Completely Fair Scheduler) algorithm:
- All ready threads sorted by `vruntime` in a **red-black tree**
- Each scheduling decision picks the thread with the smallest `vruntime`
- `vruntime` increment = actual runtime × 1024 / weight
- Weight determined by nice value lookup (nice=0 → weight=1024)
- Timer interrupt at 100Hz, scheduling latency 30ms, minimum granularity 10ms

### DMA Driver

- `dma_init()` resets the 8237A controller flip-flops during boot and masks programmable channels by default
- The exported API includes `dma_channel_setup()`, `dma_channel_mask()`, `dma_channel_unmask()`, and `dma_buffer_is_compatible()`
- The disk layer now scans the PCI IDE controller, enables the BAR4 Bus Master IDE registers, and prefers ATA `READ DMA` / `WRITE DMA` in the low-level `ide_read()` / `ide_write()` path
- IDE DMA uses a PRDT (Physical Region Descriptor Table) to split virtual buffers into page-sized physical segments, enabling scatter-gather transfers instead of PIO port copies
- If PCI BMIDE is unavailable or a disk reports no ATA DMA support, the IDE driver falls back to the original PIO path automatically
- The 8237A driver remains available for floppy, sound, or other ISA devices and still enforces the 64KB/128KB boundary and 16MB physical address rules

## License

This project is licensed under the [MIT License](LICENSE).
