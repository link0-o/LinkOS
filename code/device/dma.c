#include "dma.h"
#include "debug.h"
#include "io.h"
#include "memory.h"
#include "print.h"

/* 8237A 只能操作低端 ISA DMA 空间，并要求物理缓冲区满足严格边界约束。 */
#define DMA_PAGE_SIZE               4096
#define DMA_MAX_PHYS_ADDR           0x01000000
#define DMA_CHANNEL_CNT             8

/* 两级 8237A 控制器的屏蔽、模式和 flip-flop 复位寄存器。 */
#define DMA1_MASK_REG               0x0a
#define DMA1_MODE_REG               0x0b
#define DMA1_CLEAR_FF_REG           0x0c

#define DMA2_MASK_REG               0xd4
#define DMA2_MODE_REG               0xd6
#define DMA2_CLEAR_FF_REG           0xd8

#define DMA_MODE_SINGLE             0x40
#define DMA_MODE_ADDR_INC           0x00
#define DMA_MODE_WRITE              0x04
#define DMA_MODE_READ               0x08

/* 8237A 的地址/计数寄存器是按通道分散映射的。通道 4 是级联通道，不可编程。 */
static const uint16_t dma_addr_ports[DMA_CHANNEL_CNT] = {
    0x00, 0x02, 0x04, 0x06, 0x00, 0xc4, 0xc8, 0xcc
};

static const uint16_t dma_count_ports[DMA_CHANNEL_CNT] = {
    0x01, 0x03, 0x05, 0x07, 0x00, 0xc6, 0xca, 0xce
};

static const uint16_t dma_page_ports[DMA_CHANNEL_CNT] = {
    0x87, 0x83, 0x81, 0x82, 0x00, 0x8b, 0x89, 0x8a
};

/* 通道 0~3 是 8 位通道，5~7 是 16 位通道，4 用作主从级联。 */
static bool dma_channel_valid(uint8_t channel) {
    return channel < DMA_CHANNEL_CNT && channel != 4;
}

static bool dma_channel_is_16bit(uint8_t channel) {
    return channel >= 5;
}

static uint16_t dma_mask_port(uint8_t channel) {
    return channel < 4 ? DMA1_MASK_REG : DMA2_MASK_REG;
}

static uint16_t dma_mode_port(uint8_t channel) {
    return channel < 4 ? DMA1_MODE_REG : DMA2_MODE_REG;
}

static uint16_t dma_clear_ff_port(uint8_t channel) {
    return channel < 4 ? DMA1_CLEAR_FF_REG : DMA2_CLEAR_FF_REG;
}

/* 8237A 内部通过 flip-flop 交替接收低/高字节。
 * 每次重写地址或计数前都要先复位，否则低高字节顺序会错。 */
static void dma_reset_flip_flop(uint8_t channel) {
    outb(dma_clear_ff_port(channel), 0xff);
}

/* 第二片控制器里，通道号在模式寄存器中仍然只占 2 位。 */
static uint8_t dma_local_channel(uint8_t channel) {
    return channel & 0x03;
}

/* 8237A 只能处理物理连续的缓冲区。
 * 内核分配出来的虚拟页可能映射到离散物理页，所以这里逐页检查。 */
static bool dma_region_contiguous(uint32_t vaddr, uint32_t size, uint32_t* phys_addr) {
    uint32_t start = addr_v2p(vaddr);
    uint32_t offset = 0;

    while (offset < size) {
        uint32_t cur_vaddr = vaddr + offset;
        uint32_t cur_paddr = addr_v2p(cur_vaddr);
        uint32_t bytes_this_page = DMA_PAGE_SIZE - (cur_vaddr & (DMA_PAGE_SIZE - 1));
        uint32_t chunk = size - offset;

        if (chunk > bytes_this_page) {
            chunk = bytes_this_page;
        }
        if (cur_paddr != start + offset) {
            return false;
        }
        offset += chunk;
    }

    *phys_addr = start;
    return true;
}

/* 检查 8237A 的经典边界限制。
 * 8 位通道不能跨 64KB 边界；
 * 16 位通道以字为单位编址，不能跨 128KB 边界，且地址/长度都必须 2 字节对齐。 */
static bool dma_boundary_ok(uint8_t channel, uint32_t phys_addr, uint32_t size) {
    uint32_t end_addr;

    if (size == 0) {
        return false;
    }
    if (phys_addr >= DMA_MAX_PHYS_ADDR) {
        return false;
    }

    end_addr = phys_addr + size - 1;
    if (end_addr >= DMA_MAX_PHYS_ADDR || end_addr < phys_addr) {
        return false;
    }

    if (dma_channel_is_16bit(channel)) {
        if ((phys_addr & 0x1) != 0 || (size & 0x1) != 0) {
            return false;
        }
        return (phys_addr & 0xfffe0000) == (end_addr & 0xfffe0000);
    }

    return (phys_addr & 0xffff0000) == (end_addr & 0xffff0000);
}

bool dma_buffer_is_compatible(uint8_t channel, void* addr, uint32_t size) {
    uint32_t phys_addr;

    if (!dma_channel_valid(channel) || addr == NULL) {
        return false;
    }
    if (!dma_region_contiguous((uint32_t)addr, size, &phys_addr)) {
        return false;
    }
    return dma_boundary_ok(channel, phys_addr, size);
}

/* 屏蔽通道后，该通道不会再响应 DMA 请求。 */
void dma_channel_mask(uint8_t channel) {
    ASSERT(dma_channel_valid(channel));
    outb(dma_mask_port(channel), 0x04 | dma_local_channel(channel));
}

/* 放开通道，使外设能够发起 DMA 传输。 */
void dma_channel_unmask(uint8_t channel) {
    ASSERT(dma_channel_valid(channel));
    outb(dma_mask_port(channel), dma_local_channel(channel));
}

/* 把一次 DMA 传输写入 8237A。
 * 这里只负责控制器编程，不负责向具体外设下发“开始 DMA”的设备命令。
 * 因此它适合作为底层控制器抽象，但不能单独替换现有 IDE 驱动。现有 IDE
 * 驱动走的是 ATA PIO 数据口；若要让文件读写真正改成 DMA，需要补的是
 * PCI Bus Master IDE 或其它具体设备的 DMA 协议，而不是只写 8237A。 */
bool dma_channel_setup(uint8_t channel,
                       void* addr,
                       uint32_t size,
                       enum dma_transfer_direction direction) {
    uint32_t phys_addr;
    uint32_t count;
    uint8_t mode;

    if (!dma_channel_valid(channel) || addr == NULL || size == 0) {
        return false;
    }
    if (!dma_region_contiguous((uint32_t)addr, size, &phys_addr)) {
        return false;
    }
    if (!dma_boundary_ok(channel, phys_addr, size)) {
        return false;
    }

    dma_channel_mask(channel);
    dma_reset_flip_flop(channel);

    /* 16 位通道的地址和计数以“字”为单位编码。 */
    if (dma_channel_is_16bit(channel)) {
        count = (size >> 1) - 1;
        phys_addr >>= 1;
    } else {
        count = size - 1;
    }

    /* 先写起始地址，再写传输计数，最后写页寄存器。 */
    outb(dma_addr_ports[channel], phys_addr & 0xff);
    outb(dma_addr_ports[channel], (phys_addr >> 8) & 0xff);

    dma_reset_flip_flop(channel);
    outb(dma_count_ports[channel], count & 0xff);
    outb(dma_count_ports[channel], (count >> 8) & 0xff);
    outb(dma_page_ports[channel], (phys_addr >> 16) & 0xff);

    /* 8237A 的 READ/WRITE 是站在 DMA 控制器视角命名的：
     * 外设 -> 内存 要写内存，因此对应 DMA_MODE_WRITE；
     * 内存 -> 外设 则对应 DMA_MODE_READ。 */
    mode = DMA_MODE_SINGLE | DMA_MODE_ADDR_INC | dma_local_channel(channel);
    mode |= direction == DMA_IO_TO_MEMORY ? DMA_MODE_WRITE : DMA_MODE_READ;
    outb(dma_mode_port(channel), mode);

    dma_channel_unmask(channel);
    return true;
}

/* 启动时默认屏蔽所有通道。
 * 这样后续只有显式配置过的设备才能发起 DMA，避免历史寄存器状态造成误传输。 */
void dma_init(void) {
    uint8_t channel;

    put_str("dma_init start\n");
    for (channel = 0; channel < DMA_CHANNEL_CNT; channel++) {
        if (!dma_channel_valid(channel)) {
            continue;
        }
        dma_channel_mask(channel);
        dma_reset_flip_flop(channel);
    }
    put_str("dma_init done\n");
}