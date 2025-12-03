#!/bin/bash
# 完整编译脚本 - MBR + Loader + Kernel

DISK="/home/yangyuhang/bochs/disk/hd60M.img"

echo "=========================================="
echo "开始编译完整操作系统"
echo "=========================================="

echo ""
echo "========== 1. 编译 MBR =========="
nasm -I include/ -o mbr.bin mbr.asm || exit 1
echo "✓ mbr.bin 编译成功 ($(stat -c%s mbr.bin) 字节)"

echo ""
echo "========== 2. 编译 Loader =========="
nasm -I include/ -o loader.bin loader.asm || exit 1
echo "✓ loader.bin 编译成功 ($(stat -c%s loader.bin) 字节)"

echo ""
echo "========== 3. 编译 Kernel =========="
mkdir -p build

echo "  [1/6] 编译 main.c..."
gcc -m32 -I lib/kernel/ -I lib/ -I device/ -c -fno-builtin -o build/main.o lib/kernel/main.c || exit 1

echo "  [2/6] 编译 init.c..."
gcc -m32 -I lib/kernel/ -I lib/ -I device/ -c -fno-builtin -o build/init.o lib/kernel/init.c || exit 1

echo "  [3/6] 编译 interrupt.c..."
gcc -m32 -I lib/kernel/ -I lib/ -I device/ -c -fno-builtin -o build/interrupt.o lib/kernel/interrupt.c || exit 1

echo "  [4/6] 编译 timer.c..."
gcc -m32 -I lib/kernel/ -I lib/ -I device/ -c -fno-builtin -o build/timer.o device/timer.c || exit 1

echo "  [5/6] 编译 print.asm..."
nasm -f elf -o build/print.o lib/kernel/print.asm || exit 1

echo "  [6/6] 编译 kerner.asm (中断入口表)..."
nasm -f elf -o build/kerner.o lib/kernel/kerner.asm || exit 1

echo "  [链接] 生成 kernel.bin..."
ld -m elf_i386 -Ttext 0xc0001500 -e main -o build/kernel.bin \
    build/main.o build/init.o build/interrupt.o build/timer.o build/print.o build/kerner.o || exit 1

# 复制到 kernel 目录（保持兼容）
mkdir -p kernel
cp build/kernel.bin kernel/kernel.bin
echo "✓ kernel.bin 编译成功 ($(stat -c%s build/kernel.bin) 字节)"

echo ""
echo "========== 4. 写入磁盘镜像 =========="
echo "目标磁盘: $DISK"

if [ ! -f "$DISK" ]; then
    echo "❌ 错误: 磁盘镜像不存在: $DISK"
    exit 1
fi

echo "  [1/3] 写入 MBR (扇区 0)..."
dd if=mbr.bin of="$DISK" bs=512 count=1 conv=notrunc status=none
echo "    ✓ MBR 写入完成"

echo "  [2/3] 写入 Loader (扇区 2-5)..."
dd if=loader.bin of="$DISK" bs=512 count=4 seek=2 conv=notrunc status=none
echo "    ✓ Loader 写入完成"

echo "  [3/3] 写入 Kernel (扇区 9+)..."
dd if=build/kernel.bin of="$DISK" bs=512 count=200 seek=9 conv=notrunc status=none
echo "    ✓ Kernel 写入完成"

echo ""
echo "=========================================="
echo "✅ 编译成功！所有组件已写入磁盘"
echo "=========================================="
echo "组件列表:"
ls -lh mbr.bin loader.bin build/kernel.bin
echo ""
echo "启动命令:"
echo "  bochs -f /home/yangyuhang/bochs/bochsrc.txt -q"
