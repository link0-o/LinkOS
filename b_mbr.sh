#!/bin/bash

# ========================================
# 写入 MBR 到磁盘镜像（仅写入，不编译）
# 使用前请确保已手动执行：
#   nasm -I include/ -o mbr.bin xxx.asm
# ========================================

ASM_DIR="/home/yangyuhang/code/汇编"
BIN_FILE="$ASM_DIR/mbr.bin"
IMG_FILE="/home/yangyuhang/bochs/disk/hd60M.img"

# 检查文件是否存在
if [ ! -f "$BIN_FILE" ]; then
    echo "❌ 错误：$BIN_FILE 不存在，请先编译生成 .bin 文件"
    exit 1
fi

if [ ! -f "$IMG_FILE" ]; then
    echo "❌ 错误：磁盘镜像 $IMG_FILE 不存在"
    exit 1
fi

# 显示确认信息
echo "📝 即将写入 MBR："
echo "   源文件: $BIN_FILE"
echo "   目标镜像: $IMG_FILE"
read -p "⚠️  确认要写入吗？[y/N] " -n 1 -r
echo

if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "操作已取消"
    exit 0
fi

# 执行写入
echo "🔧 正在写入 MBR..."
dd if="$BIN_FILE" of="$IMG_FILE" bs=512 count=1 conv=notrunc status=progress

if [ $? -eq 0 ]; then
    echo "✅ 成功：MBR 已写入 $IMG_FILE"
else
    echo "❌ 错误：dd 写入失败"
    exit 1
fi
