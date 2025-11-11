# 内核加载流程文档

## 📋 完成的工作

### 1. 添加了保护模式下的内核加载功能

**关键常量** (`boot.inc`):
```asm
KERNEL_START_SECTOR equ 0x9           ; kernel.bin 起始扇区
KERNEL_BIN_BASE_ADDR equ 0x70000      ; 临时加载地址（物理）
KERNEL_ENTRY_POINT equ 0xc0001500     ; 内核入口（虚拟）
PT_NULL equ 0                          ; ELF program header 类型
```

### 2. 实现的三个关键函数

#### `rd_disk_m_32` - 保护模式磁盘读取
- 在 32 位保护模式下使用 PIO 方式读取硬盘
- 支持 LBA 寻址
- 参数：
  - `eax` = LBA 扇区号
  - `ebx` = 目标物理地址
  - `ecx` = 扇区数

#### `kernel_init` - 内核初始化
- 解析 ELF 格式的 kernel.bin
- 读取 program header table
- 将各个 segment 拷贝到正确的虚拟地址

#### `mem_cpy` - 内存拷贝
- 使用 `rep movsb` 实现高效字节拷贝
- 栈传参：`mem_cpy(dst, src, size)`

## 🚀 启动流程

```
BIOS
  ↓
MBR (扇区0)
  ├─ 初始化段寄存器
  ├─ 清屏并显示 "hello OS"
  └─ 加载 Loader 到 0x900
     ↓
Loader (扇区2-5)
  ├─ 检测物理内存（E820/E801/0x88）
  ├─ 进入保护模式，显示 'P'
  ├─ 【在保护模式下】加载 kernel.bin 到 0x70000，显示 'K'
  ├─ 初始化分页机制
  ├─ 迁移 GDT 到高地址，显示 'V'
  ├─ 解析 ELF 并加载内核到虚拟地址，显示 'I'
  └─ 跳转到 kernel main (0xc0001500)
     ↓
Kernel (扇区9-18)
  └─ main() 函数开始执行
```

## 💾 内存布局

| 物理地址 | 用途 |
|----------|------|
| 0x00000000 | 低 1MB：MBR、Loader、页表 |
| 0x00007C00 | MBR 加载位置 |
| 0x00000900 | Loader 加载位置 |
| 0x00070000 | Kernel.bin 临时加载位置 |
| 0x00100000 | 页目录表（1MB 处） |
| 0xC0001500 | Kernel 虚拟入口点（映射到物理地址） |

## 📝 编译和运行

### 编译内核
```bash
cd ~/code/汇编/kernel
gcc -m32 -c -o main.o main.c
ld -m elf_i386 -Ttext 0xc0001500 -e main -o kernel.bin main.o
```

### 写入镜像
```bash
cd ~/code/汇编
dd if=mbr.bin of=~/bochs/disk/hd60M.img bs=512 count=1 conv=notrunc
dd if=loader.bin of=~/bochs/disk/hd60M.img bs=512 count=4 seek=2 conv=notrunc
dd if=kernel/kernel.bin of=~/bochs/disk/hd60M.img bs=512 count=10 seek=9 conv=notrunc
```

### 运行
```bash
cd ~/bochs
bochs -f bochsrc.txt
```

## 🎯 预期显示

屏幕第一行应该显示：
- `hello OS` （MBR）

屏幕第二行应该显示：
- `P` (绿色) - 进入保护模式
- `K` (红色) - 内核加载完成
- `V` (绿色) - 分页启用+GDT迁移
- `I` (黄色) - 内核初始化完成

然后内核 `main()` 函数开始执行（死循环）。

## 🔍 调试技巧

### 在 Bochs 中查看内核是否正确加载
```
pb 0x70000        # 内核临时加载地址
c
x /20xb 0x70000   # 查看 ELF header（应以 7f 45 4c 46 开头）

pb 0xc0001500     # 内核入口点
c
r                 # 查看寄存器
```

### 查看 ELF header
```bash
readelf -h kernel/kernel.bin
readelf -l kernel/kernel.bin   # 查看 program headers
```

## ⚠️ 注意事项

1. **MBR 读取扇区数不够**：如果 Loader 超过 2KB，需要修改 `3mbr.asm` 中的 `mov cx, 4`

2. **内核大小限制**：当前读取 200 个扇区（100KB），如果内核更大需要增加

3. **虚拟地址映射**：确保页表正确映射了内核所在的虚拟地址范围

4. **段寄存器重载**：GDT 迁移后必须重新加载所有段寄存器

## 🎉 完成状态

✅ 保护模式下内核加载功能已完成
✅ ELF 解析和段加载已实现
✅ 内存拷贝函数已实现
✅ 所有文件已编译并写入镜像

现在可以启动 Bochs 测试了！
