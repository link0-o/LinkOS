
#include "ide.h"
#include "stdint.h"
#include "global.h"
#include "debug.h"
#include "interrupt.h"
#include "timer.h"
#include "io.h"
#include "stdio-kernel.h"
#include "stdio.h"
#include "string.h"
#include "list.h"

/* 内核堆内存分配（定义于 kernel/syscall.c，头文件 kernel/syscall.h）*/
extern void* sys_malloc(uint32_t size);
extern void  sys_free(void* ptr);

extern void* get_kernel_pages(uint32_t pg_cnt);
extern void  mfree_page(enum pool_flags pf, void* _vaddr, uint32_t pg_cnt);
extern uint32_t addr_v2p(uint32_t vaddr);

/* 定义硬盘各寄存器的端口号（相对于通道起始端口的偏移） */
#define reg_data(channel)       (channel->port_base + 0)   // 数据寄存器（16位）
#define reg_error(channel)      (channel->port_base + 1)   // 错误寄存器（读）/ 特征寄存器（写）
#define reg_feature(channel)    reg_error(channel)
#define reg_sect_cnt(channel)   (channel->port_base + 2)   // 扇区计数寄存器
#define reg_lba_l(channel)      (channel->port_base + 3)   // LBA 低 8 位（bit 0~7）
#define reg_lba_m(channel)      (channel->port_base + 4)   // LBA 中 8 位（bit 8~15）
#define reg_lba_h(channel)      (channel->port_base + 5)   // LBA 高 8 位（bit 16~23）
#define reg_dev(channel)        (channel->port_base + 6)   // device 寄存器
#define reg_status(channel)     (channel->port_base + 7)   // 状态寄存器（读）
#define reg_cmd(channel)        (reg_status(channel))      // 命令寄存器（写，与状态寄存器同端口）
#define reg_alt_status(channel) (channel->port_base + 0x206) // 备用状态寄存器（读）/ 设备控制寄存器（写）
#define reg_ctl(channel)        reg_alt_status(channel)

/* reg_alt_status / reg_status 寄存器的关键位 */
#define BIT_STAT_BSY            0x80  // 硬盘忙（Busy）
#define BIT_STAT_DRDY           0x40  // 驱动器准备好（Device Ready）
#define BIT_STAT_DF             0x20  // 设备错误（Device Fault）
#define BIT_STAT_DRQ            0x08  // 数据传输准备好（Data Request）
#define BIT_STAT_ERR            0x01  // 错误位

/* device 寄存器的关键位 */
#define BIT_DEV_MBS             0xa0  // 第 7 位和第 5 位固定为 1（Must Be Set）
#define BIT_DEV_LBA             0x40  // LBA 寻址模式（第 6 位）
#define BIT_DEV_DEV             0x10  // 硬盘选择：0 = 主盘，1 = 从盘（第 4 位）

/* 硬盘操作命令 */
#define CMD_IDENTIFY            0xec  // IDENTIFY 命令：获取硬盘身份信息
#define CMD_READ_SECTOR         0x20  // READ SECTOR(S) 命令：读扇区
#define CMD_WRITE_SECTOR        0x30  // WRITE SECTOR(S) 命令：写扇区
#define CMD_READ_DMA            0xc8  // READ DMA 命令：通过 BMIDE DMA 读扇区
#define CMD_WRITE_DMA           0xca  // WRITE DMA 命令：通过 BMIDE DMA 写扇区

/* PCI config mechanism #1 寄存器 */
#define PCI_CONFIG_ADDR         0xcf8
#define PCI_CONFIG_DATA         0xcfc

/* PCI 设备筛选：Mass Storage / IDE */
#define PCI_CLASS_MASS_STORAGE  0x01
#define PCI_SUBCLASS_IDE        0x01

/* PCI 配置空间偏移 */
#define PCI_REG_CLASS_CODE      0x08
#define PCI_REG_COMMAND         0x04
#define PCI_REG_BAR4            0x20

/* PCI command 寄存器位 */
#define PCI_CMD_IO_SPACE        0x01
#define PCI_CMD_BUS_MASTER      0x04

/* BMIDE 寄存器布局：每个通道占 8 字节 */
#define reg_bm_cmd(channel)     (channel->dma_base + 0)
#define reg_bm_status(channel)  (channel->dma_base + 2)
#define reg_bm_prdt(channel)    (channel->dma_base + 4)

/* BMIDE command/status 位 */
#define BM_CMD_START            0x01
#define BM_CMD_READ             0x08  // 1 = 磁盘 -> 内存, 0 = 内存 -> 磁盘
#define BM_STATUS_ACTIVE        0x01
#define BM_STATUS_ERROR         0x02
#define BM_STATUS_INTR          0x04

#define IDE_PRDT_MAX_ENTRIES    256
#define IDE_PRD_EOT             0x8000
#define IDE_DMA_MAX_BYTES       (256 * 512)

/* 支持的最大 LBA 地址（调试用，限制在 80MB 以内） */
#define max_lba                 ((80 * 1024 * 1024 / 512) - 1)

/* 全局变量 */
uint8_t channel_cnt;                // ATA 通道数
struct ide_channel channels[2];     // 两个 ATA 通道的数据

/* 分区表扫描辅助变量 */
static int32_t ext_lba_base = 0;   // 总扩展分区的起始 LBA（为 0 表示尚未找到）
static uint8_t p_no = 0;           // 主分区下标计数
static uint8_t l_no = 0;           // 逻辑分区下标计数

struct list partition_list;         // 所有分区的链表（供文件系统使用）

/* PCI IDE Bus Master 的 PRDT 项。一个项描述一段物理连续缓冲区。 */
struct ide_prd_entry {
    uint32_t phys_addr;
    uint16_t byte_count;
    uint16_t flags;
} __attribute__((packed));

/* 分区表项结构（16 字节） */
struct partition_table_entry {
    uint8_t  bootable;              // 是否可引导：0x80 = 可引导，0x00 = 不可引导
    uint8_t  start_head;            // 起始磁头号（CHS 寻址，已基本废弃）
    uint8_t  start_sec;             // 起始扇区号（CHS，1-based）
    uint8_t  start_chs;             // 起始柱面号（CHS）
    uint8_t  fs_type;               // 文件系统类型（0x00=空，0x05/0x0F=扩展分区，0x83=Linux）
    uint8_t  end_head;              // 结束磁头号
    uint8_t  end_sec;               // 结束扇区号
    uint8_t  end_chs;               // 结束柱面号
    uint32_t start_lba;             // 本分区起始扇区的 LBA 地址（重要）
    uint32_t sec_cnt;               // 本分区的扇区总数（重要）
} __attribute__((packed));          // 禁止编译器填充对齐空隙，保证严格 16 字节

/* 引导扇区结构（MBR 或 EBR 所在扇区，512 字节） */
struct boot_sector {
    uint8_t other[446];                             // 引导代码（446 字节）
    struct partition_table_entry partition_table[4]; // 分区 表（4 项，64 字节）
    uint16_t signature;                             // 结束标志：0x55AA（小端序为 0xAA55）
} __attribute__((packed));

/******* 内部辅助函数 *******/

/* 选择待操作的硬盘（通过 device 寄存器的 DEV 位选主/从盘） */
static void select_disk(struct disk* hd) {
    uint8_t reg_device = BIT_DEV_MBS | BIT_DEV_LBA;
    if (hd->dev_no == 1) {
        reg_device |= BIT_DEV_DEV;  // dev_no=1 表示从盘，置位 DEV 位
    }
    outb(reg_dev(hd->my_channel), reg_device);
}

/* 向硬盘控制器写入起始扇区 LBA 地址及待读写的扇区数 */
static void select_sector(struct disk* hd, uint32_t lba, uint8_t sec_cnt) {
    ASSERT(lba <= max_lba);
    struct ide_channel* channel = hd->my_channel;

    /* 写扇区计数（0 表示 256 个扇区） */
    outb(reg_sect_cnt(channel), sec_cnt);

    /* 写 LBA 地址（28 位）：低 24 位分三个字节写入，高 4 位写入 device 寄存器 */
    outb(reg_lba_l(channel),  lba);           // LBA bit 0~7
    outb(reg_lba_m(channel),  lba >> 8);      // LBA bit 8~15
    outb(reg_lba_h(channel),  lba >> 16);     // LBA bit 16~23

    /* 重写 device 寄存器，同时写入 LBA bit 24~27（高 4 位）及主/从盘选择 */
    outb(reg_dev(channel), BIT_DEV_MBS | BIT_DEV_LBA |
         (hd->dev_no == 1 ? BIT_DEV_DEV : 0) | (lba >> 24));
}

/* 向通道发送命令，并将 expecting_intr 置 true，告知中断处理程序此次中断是期待的 */
static void cmd_out(struct ide_channel* channel, uint8_t cmd) {
    channel->expecting_intr = true;
    outb(reg_cmd(channel), cmd);
}

/* 通过 PCI config mechanism #1 读取配置空间双字 */
static uint32_t pci_config_read_dword(uint8_t bus, uint8_t device,
                                      uint8_t function, uint8_t reg) {
    uint32_t addr = 0x80000000 | ((uint32_t)bus << 16) |
                    ((uint32_t)device << 11) | ((uint32_t)function << 8) |
                    (reg & 0xfc);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

/* 向 PCI 配置空间写入双字 */
static void pci_config_write_dword(uint8_t bus, uint8_t device,
                                   uint8_t function, uint8_t reg,
                                   uint32_t value) {
    uint32_t addr = 0x80000000 | ((uint32_t)bus << 16) |
                    ((uint32_t)device << 11) | ((uint32_t)function << 8) |
                    (reg & 0xfc);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, value);
}

/* 在 PCI 总线上寻找 IDE 控制器，并打开 I/O Space + Bus Master。
 * Bochs 的 i440FX/PIIX IDE 会通过 BAR4 暴露 BMIDE 寄存器。 */
static uint16_t pci_find_ide_bmibase(void) {
    uint8_t device;
    uint8_t function;

    for (device = 0; device < 32; device++) {
        for (function = 0; function < 8; function++) {
            uint32_t vendor_device = pci_config_read_dword(0, device, function, 0x00);
            uint32_t class_reg;
            uint32_t bar4;
            uint32_t cmd_reg;

            if ((vendor_device & 0xffff) == 0xffff) {
                if (function == 0) {
                    break;
                }
                continue;
            }

            class_reg = pci_config_read_dword(0, device, function, PCI_REG_CLASS_CODE);
            if (((class_reg >> 24) & 0xff) != PCI_CLASS_MASS_STORAGE ||
                ((class_reg >> 16) & 0xff) != PCI_SUBCLASS_IDE) {
                continue;
            }

            cmd_reg = pci_config_read_dword(0, device, function, PCI_REG_COMMAND);
            cmd_reg |= PCI_CMD_IO_SPACE | PCI_CMD_BUS_MASTER;
            pci_config_write_dword(0, device, function, PCI_REG_COMMAND, cmd_reg);

            bar4 = pci_config_read_dword(0, device, function, PCI_REG_BAR4);
            if ((bar4 & 0x1) == 0) {
                return 0;
            }
            printk("   pci ide controller: %x:%x bmide=0x%x\n",
                   device, function, bar4 & 0xfff0);
            return bar4 & 0xfff0;
        }
    }
    return 0;
}

/* BMIDE 完成后不要求 DRQ=1，因此单独检查 DMA 结束状态。 */
static bool dma_wait(struct disk* hd) {
    struct ide_channel* channel = hd->my_channel;
    uint16_t time_limit = 30 * 1000;

    while (time_limit -= 10 >= 0) {
        uint8_t status = inb(reg_status(channel));
        if (!(status & BIT_STAT_BSY)) {
            return !(status & (BIT_STAT_ERR | BIT_STAT_DF));
        }
        mtime_sleep(10);
    }
    return false;
}

/* 把任意虚拟缓冲区拆成 PRDT。
 * 每个 PRD 只描述当前页里的连续物理范围，避免跨页假定物理连续。 */
static bool ide_build_prdt(struct ide_channel* channel, void* buf, uint32_t bytes) {
    struct ide_prd_entry* prdt = (struct ide_prd_entry*)channel->prdt;
    uint32_t vaddr = (uint32_t)buf;
    uint32_t bytes_left = bytes;
    uint32_t prd_idx = 0;

    ASSERT(channel->prdt != NULL);
    memset(prdt, 0, 4096);

    while (bytes_left > 0) {
        uint32_t phys_addr = addr_v2p(vaddr);
        uint32_t page_left = 4096 - (phys_addr & 0xfff);
        uint32_t chunk = bytes_left < page_left ? bytes_left : page_left;

        if (prd_idx >= IDE_PRDT_MAX_ENTRIES) {
            return false;
        }

        prdt[prd_idx].phys_addr = phys_addr;
        prdt[prd_idx].byte_count = chunk == 0x10000 ? 0 : chunk;
        prdt[prd_idx].flags = 0;

        bytes_left -= chunk;
        vaddr += chunk;
        prd_idx++;
    }

    prdt[prd_idx - 1].flags = IDE_PRD_EOT;
    return true;
}

/* 配置单个通道的 BMIDE 资源。 */
static void ide_dma_channel_init(struct ide_channel* channel, uint8_t channel_no,
                                 uint16_t bmide_base) {
    channel->dma_base = bmide_base + channel_no * 8;
    channel->prdt = get_kernel_pages(1);
    if (channel->prdt == NULL) {
        channel->dma_enabled = false;
        channel->dma_base = 0;
        printk("   %s dma disabled: no memory for prdt\n", channel->name);
        return;
    }

    channel->prdt_phys = addr_v2p((uint32_t)channel->prdt);
    channel->dma_enabled = true;
    outb(reg_bm_cmd(channel), 0x00);
    outb(reg_bm_status(channel), BM_STATUS_ERROR | BM_STATUS_INTR);
    printk("   %s dma base: 0x%x prdt=0x%x\n",
           channel->name, channel->dma_base, channel->prdt_phys);
}

/* 通过 PCI IDE Bus Master 读/写扇区。
 * 若通道或设备不支持 DMA，则返回 false 让上层退回 PIO。 */
static bool ide_dma_transfer(struct disk* hd, uint32_t lba, void* buf,
                             uint8_t sec_cnt, bool is_write) {
    struct ide_channel* channel = hd->my_channel;
    uint32_t bytes = sec_cnt == 0 ? IDE_DMA_MAX_BYTES : sec_cnt * 512;
    uint8_t bm_cmd = is_write ? 0 : BM_CMD_READ;
    uint8_t bm_status;

    if (!channel->dma_enabled || !hd->dma_supported) {
        return false;
    }
    if (!ide_build_prdt(channel, buf, bytes)) {
        return false;
    }

    outb(reg_bm_cmd(channel), bm_cmd);
    outl(reg_bm_prdt(channel), channel->prdt_phys);
    outb(reg_bm_status(channel), BM_STATUS_ERROR | BM_STATUS_INTR);

    select_disk(hd);
    select_sector(hd, lba, sec_cnt);
    outb(reg_feature(channel), 0x00);

    cmd_out(channel, is_write ? CMD_WRITE_DMA : CMD_READ_DMA);
    outb(reg_bm_cmd(channel), bm_cmd | BM_CMD_START);

    sema_down(&channel->disk_done);
    outb(reg_bm_cmd(channel), bm_cmd);

    bm_status = inb(reg_bm_status(channel));
    outb(reg_bm_status(channel), BM_STATUS_ERROR | BM_STATUS_INTR);

    if ((bm_status & BM_STATUS_ERROR) || !(bm_status & BM_STATUS_INTR)) {
        return false;
    }
    if (!dma_wait(hd)) {
        return false;
    }
    return true;
}
 
/* 从硬盘缓冲区读取 sec_cnt 个扇区的数据到内存 buf */
static void read_from_sector(struct disk* hd, void* buf, uint8_t sec_cnt) {
    uint32_t size_in_byte;
    if (sec_cnt == 0) {
        /* sec_cnt 为 8 位变量，由调用方传入 256 时高位溢出为 0，视为 256 扇区 */
        size_in_byte = 256 * 512;
    } else {
        size_in_byte = sec_cnt * 512;
    }
    insw(reg_data(hd->my_channel), buf, size_in_byte / 2);
}

/* 将内存 buf 中 sec_cnt 个扇区的数据写入硬盘缓冲区 */
static void write2sector(struct disk* hd, void* buf, uint8_t sec_cnt) {
    uint32_t size_in_byte;
    if (sec_cnt == 0) {
        size_in_byte = 256 * 512;
    } else {
        size_in_byte = sec_cnt * 512;
    }
    outsw(reg_data(hd->my_channel), buf, size_in_byte / 2);
}

/* 等待硬盘完成操作，最多等待 30 秒
 * 返回 true 表示硬盘已就绪（DRQ 位为 1），false 表示超时 */
static bool busy_wait(struct disk* hd) {
    struct ide_channel* channel = hd->my_channel;
    uint16_t time_limit = 30 * 1000;   // 30 秒，单位毫秒
    while (time_limit -= 10 >= 0) {
        if (!(inb(reg_status(channel)) & BIT_STAT_BSY)) {
            /* 硬盘不忙，返回 DRQ 位（数据是否准备好） */
            return (inb(reg_status(channel)) & BIT_STAT_DRQ);
        } else {
            mtime_sleep(10);    // 硬盘忙，休眠 10ms 后再查询
        }
    }
    return false;   // 超时
}

/******* 公共接口 *******/

/* 从硬盘 hd 的 lba 扇区起读取 sec_cnt 个扇区到 buf */
void ide_read(struct disk* hd, uint32_t lba, void* buf, uint32_t sec_cnt) {
    ASSERT(lba <= max_lba);
    ASSERT(sec_cnt > 0);
    lock_acquire(&hd->my_channel->lock);

    /* 步骤 1：选择操作的硬盘（主/从盘） */
    select_disk(hd);

    uint32_t secs_op;               // 本次操作的扇区数（最多 256）
    uint32_t secs_done = 0;         // 已完成的扇区数

    while (secs_done < sec_cnt) {
        /* 每次最多读 256 个扇区（寄存器为 8 位，0 代表 256） */
        if ((secs_done + 256) <= sec_cnt) {
            secs_op = 256;
        } else {
            secs_op = sec_cnt - secs_done;
        }

        /* 步骤 2：写入起始扇区号和扇区数 */
        if (hd->my_channel->dma_enabled && hd->dma_supported) {
            if (!ide_dma_transfer(hd, lba + secs_done,
                                  (void*)((uint32_t)buf + secs_done * 512),
                                  secs_op, false)) {
                char error[64];
                sprintf(error, "%s dma read sector %d failed!\n", hd->name, lba + secs_done);
                PANIC(error);
            }
        } else {
            select_sector(hd, lba + secs_done, secs_op);
            cmd_out(hd->my_channel, CMD_READ_SECTOR);
            sema_down(&hd->my_channel->disk_done);

            if (!busy_wait(hd)) {
                char error[64];
                sprintf(error, "%s read sector %d failed!\n", hd->name, lba);
                PANIC(error);
            }
            read_from_sector(hd, (void*)((uint32_t)buf + secs_done * 512), secs_op);
        }
        secs_done += secs_op;
    }
    lock_release(&hd->my_channel->lock);
}

/* 将 buf 中 sec_cnt 个扇区的数据写入硬盘 hd 的 lba 扇区 */
void ide_write(struct disk* hd, uint32_t lba, void* buf, uint32_t sec_cnt) {
    ASSERT(lba <= max_lba);
    ASSERT(sec_cnt > 0);
    lock_acquire(&hd->my_channel->lock);

    /* 步骤 1：选择操作的硬盘 */
    select_disk(hd);

    uint32_t secs_op;
    uint32_t secs_done = 0;

    while (secs_done < sec_cnt) {
        if ((secs_done + 256) <= sec_cnt) {
            secs_op = 256;
        } else {
            secs_op = sec_cnt - secs_done;
        }

        if (hd->my_channel->dma_enabled && hd->dma_supported) {
            if (!ide_dma_transfer(hd, lba + secs_done,
                                  (void*)((uint32_t)buf + secs_done * 512),
                                  secs_op, true)) {
                char error[64];
                sprintf(error, "%s dma write sector %d failed!\n", hd->name, lba + secs_done);
                PANIC(error);
            }
        } else {
            select_sector(hd, lba + secs_done, secs_op);
            cmd_out(hd->my_channel, CMD_WRITE_SECTOR);

            if (!busy_wait(hd)) {
                char error[64];
                sprintf(error, "%s write sector %d failed!\n", hd->name, lba);
                PANIC(error);
            }

            write2sector(hd, (void*)((uint32_t)buf + secs_done * 512), secs_op);
            sema_down(&hd->my_channel->disk_done);
        }
        secs_done += secs_op;
    }
    lock_release(&hd->my_channel->lock);
}

/* 硬盘中断处理程序（IRQ14 对应 0x2e，IRQ15 对应 0x2f） */
void intr_hd_handler(uint8_t irq_no) {
    ASSERT(irq_no == 0x2e || irq_no == 0x2f);
    uint8_t ch_no = irq_no - 0x2e;         // 通道索引：IRQ14→0，IRQ15→1
    struct ide_channel* channel = &channels[ch_no];
    ASSERT(channel->irq_no == irq_no);

    /* 只处理由驱动程序主动触发的中断（expecting_intr=true）
     * 其他情况（如硬盘自检告警）暂不处理 */
    if (channel->expecting_intr) {
        channel->expecting_intr = false;
        sema_up(&channel->disk_done);   // 唤醒等待中的驱动程序

        /* 读取 status 寄存器，通知硬盘控制器本次中断已处理
         * 不读 status，硬盘控制器将不再产生新的中断 */
        inb(reg_status(channel));
    }
}

/******* 用于扫描分区表的辅助函数 *******/

/* 将 dst 中 len 个字节以相邻两字节互换位置后写入 buf
 * 用于处理 IDENTIFY 命令返回的字符串（硬盘序列号、型号等字节序） */
static void swap_pairs_bytes(const char* dst, char* buf, uint32_t len) {
    uint8_t idx;
    for (idx = 0; idx < len; idx += 2) {
        buf[idx + 1] = *dst++;
        buf[idx]     = *dst++;
    }
    buf[idx] = '\0';
}

/* 向硬盘发送 IDENTIFY 命令，读取并打印硬盘的序列号、型号和容量 */
static void identify_disk(struct disk* hd) {
    char id_info[512];      // IDENTIFY 命令返回的数据缓冲区，512 字节（1 扇区）
    lock_acquire(&hd->my_channel->lock);
    select_disk(hd);
    cmd_out(hd->my_channel, CMD_IDENTIFY);

    /* 阻塞自己，等待硬盘处理完毕后由中断处理程序唤醒 */
    sema_down(&hd->my_channel->disk_done);

    if (!busy_wait(hd)) {
        char error[64];
        sprintf(error, "%s identify failed!\n", hd->name);
        PANIC(error);
    }
    read_from_sector(hd, id_info, 1);

    char buf[64];
    /* IDENTIFY 返回数据以字为单位，且每个字中两字节互换，需要用 swap_pairs_bytes 转换 */
    uint8_t sn_start = 10 * 2, sn_len = 20;    // 序列号：字偏移 10，长度 20 字节
    uint8_t md_start = 27 * 2, md_len = 40;    // 型号  ：字偏移 27，长度 40 字节

    swap_pairs_bytes(&id_info[sn_start], buf, sn_len);
    printk("   disk %s info:\n      SN: %s\n", hd->name, buf);

    memset(buf, 0, sizeof(buf));
    swap_pairs_bytes(&id_info[md_start], buf, md_len);
    printk("      MODULE: %s\n", buf);

    uint32_t sectors = *(uint32_t*)&id_info[60 * 2];   // 字偏移 60~61，共 32 位
    uint16_t* id_words = (uint16_t*)id_info;
    hd->dma_supported = (id_words[49] & 0x0100) || id_words[63] || id_words[88];
    printk("      SECTORS: %d\n", sectors);
    printk("      CAPACITY: %dMB\n", sectors * 512 / 1024 / 1024);
    printk("      DMA: %s\n", hd->dma_supported ? "supported" : "pio-only");
    lock_release(&hd->my_channel->lock);
}

/* 递归扫描硬盘 hd 上地址为 ext_lba 的扇区中的分区表
 * ext_lba=0 表示从 MBR 开始扫描；非 0 表示从某个 EBR 扫描
 *
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │  MBR @ LBA 0                                                            │
 * │ ┌──────────┬──────────┬──────────┬──────────────────────────────────┐  │
 * │ │ 主分区1  │ 主分区2  │ 主分区3  │          扩展分区                │  │
 * │ │ fs=0x83  │ fs=0x83  │ fs=0x83  │          fs=0x05                 │  │
 * │ │ LBA绝对  │ LBA绝对  │ LBA绝对  │       start=500(绝对)            │  │
 * │ └──────────┴──────────┴──────────┴──────────────┬───────────────────┘  │
 * └──────────────────────────────────────────────────│────────────────────-─┘
 *                                                    │
 *                                                    ▼
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │  EBR1 @ LBA 500                                                         │
 * │ ┌───────────────────────┬──────────────────────┬──────────┬──────────┐  │
 * │ │       第1项           │        第2项         │  第3项   │  第4项   │  │
 * │ │      fs=0x83          │       fs=0x05        │ fs=0x00  │ fs=0x00  │  │
 * │ │  start=1(相对EBR1)    │ start=200(相对base)  │   空     │   空     │  │
 * │ │  →逻辑分区5在LBA 501  │   →下一EBR在LBA 700  │          │          │  │
 * │ └───────────────────────┴──────────┬───────────┴──────────┴──────────┘  │
 * └────────────────────────────────────│───────────────────────────────────-─┘
 *                                      │
 *                                      ▼
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │  EBR2 @ LBA 700                                                         │
 * │ ┌───────────────────────┬──────────────────────┬──────────┬──────────┐  │
 * │ │       第1项           │        第2项         │  第3项   │  第4项   │  │
 * │ │      fs=0x83          │       fs=0x05        │ fs=0x00  │ fs=0x00  │  │
 * │ │  start=1(相对EBR2)    │ start=400(相对base)  │   空     │   空     │  │
 * │ │  →逻辑分区6在LBA 701  │   →下一EBR在LBA 900  │          │          │  │
 * │ └───────────────────────┴──────────┬───────────┴──────────┴──────────┘  │
 * └────────────────────────────────────│───────────────────────────────────-─┘
 *                                      │
 *                                      ▼
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │  EBR3 @ LBA 900                                                         │
 * │ ┌───────────────────────┬──────────────────────┬──────────┬──────────┐  │
 * │ │       第1项           │        第2项         │  第3项   │  第4项   │  │
 * │ │      fs=0x83          │       fs=0x00        │ fs=0x00  │ fs=0x00  │  │
 * │ │  start=1(相对EBR3)    │     空 = 链表终点    │   空     │   空     │  │
 * │ │  →逻辑分区7在LBA 901  │                      │          │          │  │
 * │ └───────────────────────┴──────────────────────┴──────────┴──────────┘  │
 * └─────────────────────────────────────────────────────────────────────────┘
 */
static void partition_scan(struct disk* hd, uint32_t ext_lba) {
    /* 用堆内存存储引导扇区（避免递归时栈溢出） */
    struct boot_sector* bs = sys_malloc(sizeof(struct boot_sector));
    ide_read(hd, ext_lba, bs, 1);

    uint8_t part_idx = 0;
    struct partition_table_entry* p = bs->partition_table;

    /* 遍历分区表中的 4 个分区表项 */
    while (part_idx++ < 4) {
        if (p->fs_type == 0x05 || p->fs_type == 0x0F) {
            /* 扩展分区（0x05=CHS扩展，0x0F=LBA扩展） */
            if (ext_lba_base != 0) {
                /* 子扩展分区的 start_lba 是相对于总扩展分区起始地址的相对值 */
                partition_scan(hd, p->start_lba + ext_lba_base);
            } else {
                /* 第一次扫描（MBR），记录总扩展分区的绝对 LBA */
                ext_lba_base = p->start_lba;
                partition_scan(hd, p->start_lba);
            }
        } else if (p->fs_type != 0) {
            /* 有效的非扩展分区 */
            if (ext_lba == 0) {
                /* 当前在 MBR 中，是主分区 */
                hd->prim_parts[p_no].start_lba = ext_lba + p->start_lba;
                hd->prim_parts[p_no].sec_cnt   = p->sec_cnt;
                hd->prim_parts[p_no].my_disk   = hd;
                list_append(&partition_list, &hd->prim_parts[p_no].part_tag);
                sprintf(hd->prim_parts[p_no].name, "%s%d", hd->name, p_no + 1);
                p_no++;
                ASSERT(p_no < 4);   // 主分区最多 4 个
            } else {
                /* 当前在 EBR 中，是逻辑分区
                 * 逻辑分区的起始 LBA = EBR 的 LBA + 分区表项中的相对 LBA */
                hd->logic_parts[l_no].start_lba = ext_lba + p->start_lba;
                hd->logic_parts[l_no].sec_cnt   = p->sec_cnt;
                hd->logic_parts[l_no].my_disk   = hd;
                list_append(&partition_list, &hd->logic_parts[l_no].part_tag);
                /* 逻辑分区编号从 5 开始（主分区占用 1~4） */
                sprintf(hd->logic_parts[l_no].name, "%s%d", hd->name, l_no + 5);
                l_no++;
                if (l_no >= 8) {    // 只支持 8 个逻辑分区
                    sys_free(bs);
                    return;
                }
            }
        }
        p++;
    }
    sys_free(bs);
}

/* list_traversal 回调函数：打印分区信息 */
static bool partition_info(struct list_elem* pelem, int arg UNUSED) {
    struct partition* part = elem2entry(struct partition, part_tag, pelem);
    printk("   %s  start_lba: 0x%x  sec_cnt: 0x%x\n",
           part->name, part->start_lba, part->sec_cnt);
    /* 返回 false 让 list_traversal 继续遍历 */
    return false;
}

/* 硬盘驱动初始化
 * 1. 根据 BIOS 数据区 0x475 获取磁盘数量
 * 2. 初始化每个 ATA 通道的端口、中断号、锁、信号量
 * 3. 获取每块磁盘的 IDENTIFY 参数
 * 4. 扫描第二块磁盘（sdb）上的分区表
 * 5. 注册硬盘中断处理程序
 */
void ide_init(void) {
    printk("ide_init start\n");
    uint16_t bmide_base = pci_find_ide_bmibase();

    /* BIOS 数据区 0x475 存放着系统检测到的硬盘数量 */
    uint8_t hd_cnt = *((uint8_t*)(0x475));
    ASSERT(hd_cnt > 0);
    printk("   ide: detected %d hard disk(s)\n", hd_cnt);

    /* 根据硬盘数推算 ATA 通道数（每个通道最多 2 块硬盘） */
    channel_cnt = DIV_ROUND_UP(hd_cnt, 2);

    /* 初始化所有分区链表 */
    list_init(&partition_list);

    struct ide_channel* channel;
    uint8_t channel_no = 0;
    uint8_t dev_no = 0;

    /* 初始化每个 ATA 通道 */
    while (channel_no < channel_cnt) {
        channel = &channels[channel_no];
        sprintf(channel->name, "ide%d", channel_no);

        /* 设置通道的起始端口和中断向量号 */
        switch (channel_no) {
            case 0:
                channel->port_base = 0x1f0;         // Primary 通道命令块寄存器起始端口
                channel->irq_no    = 0x20 + 14;     // IRQ14 → 中断向量号 0x2E
                break;
            case 1:
                channel->port_base = 0x170;         // Secondary 通道命令块寄存器起始端口
                channel->irq_no    = 0x20 + 15;     // IRQ15 → 中断向量号 0x2F
                break;
        }

        channel->dma_base = 0;
        channel->prdt = NULL;
        channel->prdt_phys = 0;
        channel->dma_enabled = false;
        channel->expecting_intr = false;
        lock_init(&channel->lock);

        /* 信号量初始值为 0：驱动向硬盘发命令后执行 sema_down 阻塞自己，
         * 硬盘完成后触发中断，中断处理程序执行 sema_up 唤醒驱动 */
        sema_init(&channel->disk_done, 0);

        /* 注册本通道的硬盘中断处理程序 */
        register_handler(channel->irq_no, intr_hd_handler);

        if (bmide_base != 0) {
            ide_dma_channel_init(channel, channel_no, bmide_base);
        }

        /* 扫描该通道上的所有硬盘 */
        while (dev_no < 2) {
            struct disk* hd = &channel->devices[dev_no];
            hd->my_channel = channel;
            hd->dev_no     = dev_no;
            hd->dma_supported = false;
            /* 命名规则：sda=通道0主盘，sdb=通道0从盘，sdc=通道1主盘…… */
            sprintf(hd->name, "sd%c", 'a' + channel_no * 2 + dev_no);

            /* 获取硬盘的 IDENTIFY 参数（序列号、型号、容量） */
            identify_disk(hd);

            if (dev_no != 0) {
                /* dev_no=0 是系统盘（裸盘，无分区表），跳过分区扫描 */
                p_no = 0;
                l_no = 0;
                ext_lba_base = 0;
                partition_scan(hd, 0);  // 从 MBR（LBA=0）开始扫描
            }
            dev_no++;
        }
        dev_no = 0;         // 重置设备号，为下一个通道准备
        channel_no++;
    }

    printk("\n   all partitions info:\n");
    list_traversal(&partition_list, partition_info, (int)NULL);
    printk("ide_init done\n");
}