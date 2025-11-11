#!/bin/bash
# 完整编译脚本

echo "========== 开始编译内核 =========="

# 创建 build 目录
mkdir -p build

# 编译 C 文件
echo "[1/5] 编译 main.c..."
gcc -m32 -I lib/kernel/ -I lib/ -c -fno-builtin -o build/main.o lib/kernel/main.c || exit 1

echo "[2/5] 编译 init.c..."
gcc -m32 -I lib/kernel/ -I lib/ -c -fno-builtin -o build/init.o lib/kernel/init.c || exit 1

echo "[3/5] 编译 interrupt.c..."
gcc -m32 -I lib/kernel/ -I lib/ -c -fno-builtin -o build/interrupt.o lib/kernel/interrupt.c || exit 1

# 编译汇编文件
echo "[4/5] 编译 print.asm..."
nasm -f elf -o build/print.o lib/kernel/print.asm || exit 1

echo "[4/5] 编译 kerner.asm..."
nasm -f elf -o build/kerner.o lib/kernel/kerner.asm || exit 1

# 链接
echo "[5/5] 链接 kernel.bin..."
ld -m elf_i386 -Ttext 0xc0001500 -e main -o build/kernel.bin \
    build/main.o build/init.o build/interrupt.o build/print.o build/kerner.o || exit 1

# 写入磁盘
echo "========== 写入磁盘镜像 =========="
dd if=build/kernel.bin of=~/bochs/disk/hd60M.img bs=512 count=200 seek=9 conv=notrunc

echo "========== 编译完成! =========="
ls -lh build/kernel.bin
