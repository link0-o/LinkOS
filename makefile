.PHONY : mk_dir hd clean all

BUILD_DIR = ./build
ENTRY_POINT = 0xc0001500
AS = nasm
CC = gcc
LD = ld
LIB = -I lib/ -I lib/kernel/ -I lib/usr/  -I kernel/ -I device/ -I thread/
ASFLAGS = -f elf
DISK_IMG = /home/yangyuhang/bochs/disk/hd60M.img

# -Wall 开启所有常规警告(Warnings all).    -c 只编译，不链接，生成 .o 文件		-W	开启额外警告
#-fno-builtin	禁用 gcc 内置函数（如 memcpy），避免与内核实现冲突		-Wstrict-prototypes	警告：函数声明没有指定参数类型
#-Wmissing-prototypes	警告：全局函数没有提前声明原型		-m32	强制编译为 32 位 x86 代码
CFLAGS = -m32 -Wall $(LIB) -c -fno-builtin -W #-Wstrict-prototypes -Wmissing-prototypes
#-e main	指定程序入口点为 main 函数 		-Map kernel.map	生成内存映射文件.	记录每个函数、变量的地址，调试用
#-m elf_i386	生成 32 位 ELF 格式
LDFLAGS = -m elf_i386 -Ttext $(ENTRY_POINT) -e main -Map $(BUILD_DIR)/kernel.map
OBJS = $(BUILD_DIR)/main.o $(BUILD_DIR)/init.o $(BUILD_DIR)/interrupt.o \
       $(BUILD_DIR)/timer.o $(BUILD_DIR)/kernel.o $(BUILD_DIR)/print.o \
	   $(BUILD_DIR)/debug.o $(BUILD_DIR)/memory.o $(BUILD_DIR)/bitmap.o \
	   $(BUILD_DIR)/string.o $(BUILD_DIR)/thread.o $(BUILD_DIR)/list.o \
	   $(BUILD_DIR)/rbtree.o $(BUILD_DIR)/sched.o $(BUILD_DIR)/switch.o

##############	MBR 和 Loader 编译  ###############
$(BUILD_DIR)/mbr.bin: mbr.asm
	$(AS) -I include/ -o $@ $<

$(BUILD_DIR)/loader.bin: loader.asm
	$(AS) -I include/ -o $@ $<

##############	c 代码编译	###############
$(BUILD_DIR)/main.o: kernel/main.c lib/kernel/print.h lib/stdint.h kernel/init.h \
					 lib/kernel/debug.h kernel/memory.h thread/thread.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/init.o: kernel/init.c kernel/init.h lib/kernel/print.h \
                    lib/stdint.h kernel/interrupt.h device/timer.h \
                    kernel/memory.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/interrupt.o: kernel/interrupt.c kernel/interrupt.h \
                          lib/stdint.h kernel/global.h lib/kernel/io.h lib/kernel/print.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/timer.o: device/timer.c device/timer.h lib/stdint.h\
                     lib/kernel/io.h lib/kernel/print.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/debug.o: lib/kernel/debug.c lib/kernel/debug.h \
                    lib/kernel/print.h lib/stdint.h kernel/interrupt.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/memory.o: kernel/memory.c kernel/memory.h \
					 lib/kernel/print.h lib/stdint.h kernel/global.h lib/kernel/io.h \
					 lib/kernel/debug.h lib/string.h lib/kernel/bitmap.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/bitmap.o: lib/kernel/bitmap.c lib/kernel/bitmap.h \
					 lib/stdint.h lib/string.h lib/kernel/print.h kernel/interrupt.h lib/kernel/debug.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/string.o: lib/string.c lib/string.h \
					 lib/stdint.h kernel/global.h lib/kernel/debug.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/thread.o: thread/thread.c thread/thread.h \
					 lib/stdint.h lib/string.h kernel/global.h kernel/memory.h \
					 kernel/sched.h lib/kernel/list.h lib/kernel/rbtree.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/list.o: lib/kernel/list.c lib/kernel/list.h \
					 kernel/global.h kernel/interrupt.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/rbtree.o: lib/kernel/rbtree.c lib/kernel/rbtree.h \
					 lib/stdint.h kernel/global.h
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/sched.o: kernel/sched.c kernel/sched.h \
					 thread/thread.h lib/kernel/rbtree.h lib/stdint.h \
					 kernel/global.h lib/kernel/debug.h kernel/interrupt.h \
					 lib/kernel/print.h lib/string.h
	$(CC) $(CFLAGS) $< -o $@

##############	汇编代码编译  ###############
$(BUILD_DIR)/kernel.o: kernel/kernel.asm
	$(AS) $(ASFLAGS) $< -o $@
$(BUILD_DIR)/print.o: lib/kernel/print.asm
	$(AS) $(ASFLAGS) $< -o $@
$(BUILD_DIR)/switch.o: kernel/switch.asm
	$(AS) $(ASFLAGS) $< -o $@

##############	链接所有目标文件  #############
$(BUILD_DIR)/kernel.bin: $(OBJS)
	$(LD) $(LDFLAGS) $^ -o $@

mk_dir:
	if [[ ! -d $(BUILD_DIR) ]];then mkdir $(BUILD_DIR);fi

# 分别写入 MBR、Loader、Kernel
hd_mbr: $(BUILD_DIR)/mbr.bin
	dd if=$(BUILD_DIR)/mbr.bin of=$(DISK_IMG) bs=512 count=1 conv=notrunc

hd_loader: $(BUILD_DIR)/loader.bin
	dd if=$(BUILD_DIR)/loader.bin of=$(DISK_IMG) bs=512 count=4 seek=2 conv=notrunc

hd_kernel: $(BUILD_DIR)/kernel.bin
	dd if=$(BUILD_DIR)/kernel.bin of=$(DISK_IMG) bs=512 count=200 seek=9 conv=notrunc

# 统一写盘
hd: hd_mbr hd_loader hd_kernel

clean:
	cd $(BUILD_DIR) && rm -f ./*
	@echo "clean done!"

compile: $(BUILD_DIR)/mbr.bin $(BUILD_DIR)/loader.bin $(BUILD_DIR)/kernel.bin

all: mk_dir compile hd
	@echo "build done!"
