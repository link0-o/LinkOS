#include "bitmap.h"
#include "stdint.h"
#include "string.h"
#include "print.h"
#include "interrupt.h"
#include "debug.h"

/* 初始化位图btmp */
void bitmap_init(Bitmap* btmp) {
    memset(btmp->bits, 0, btmp->btmp_bytes_len);
}

/* 判断bit_idx位是否为1,若为1则返回true,否则返回false */
bool bitmap_scan_test(Bitmap* btmp, uint32_t bit_idx) {
    uint32_t byte_idx = bit_idx / 8;          // 向下取整计算出bit_idx对应的字节下标
    uint8_t  bit_odd  = bit_idx % 8;          // 计算出bit_idx在所在字节中的偏移
    return (btmp->bits[byte_idx] & (BITMAP_MASK << bit_odd));
}

/* 将位图 btmp 的 bit_idx 位设置为 value */
void bitmap_set(Bitmap* btmp, uint32_t bit_idx, int8_t value) {
    ASSERT((value == 0) || (value == 1));
    uint32_t byte_idx = bit_idx / 8;          // 向下取整计算出bit_idx对应的字节下标
    uint8_t  bit_odd  = bit_idx % 8;          // 计算出bit_idx在所在字节中的偏移
    if (value) {
        btmp->bits[byte_idx] |= (BITMAP_MASK << bit_odd);
    } else {
        btmp->bits[byte_idx] &= ~(BITMAP_MASK << bit_odd);
    }
}

/* 在位图中申请连续 cnt 个位，成功，则返回其起始位下标，失败，返回−1 */
int bitmap_scan(Bitmap* btmp, uint32_t cnt) {
    ASSERT(cnt > 0);                

    uint32_t byte_len = cnt / 8;     // 需要的完整字节数
    uint8_t bit_len = cnt % 8;        // 剩余的位数
    uint32_t idx_byte = 0;            // 当前扫描到的字节下标
    
    if (byte_len == 0) {            //直接逐位扫描
        uint32_t now_cnt = 0;
        while (idx_byte < btmp->btmp_bytes_len) {
            // 跳过全满的字节
            if (btmp->bits[idx_byte] == 0xff) {
                idx_byte++;
                now_cnt = 0;
                continue;
            }
            
            // 逐位扫描当前字节
            for (uint8_t i = 0; i < 8; i++) {
                if (!(btmp->bits[idx_byte] & (1 << i))) {
                    now_cnt++;
                    if (now_cnt == bit_len) {
                        return (idx_byte * 8 + i - bit_len + 1);
                    }
                } else {
                    now_cnt = 0;
                }
            }
            idx_byte++;
        }
        return -1;
    }
    
    // 寻找连续 byte_len 或 byte_len-1 个连续全0字节块
    // 例如特殊情况：10000000 00000000 00000001 需要找到 byte_len-1 个连续的0，再检查两侧
    while (idx_byte < btmp->btmp_bytes_len) {
        // 跳过非全0字节
        if (btmp->bits[idx_byte] != 0x00) {
            idx_byte++;
            continue;
        }
        
        // 检查连续全0字节块
        uint32_t j;
        for (j = 0; j < byte_len && (idx_byte + j) < btmp->btmp_bytes_len; j++) {
            if (btmp->bits[idx_byte + j] != 0x00) {
                break;
            }
        }
        
        // 情况1：找到了 byte_len 个完整的全0字节
        if (j == byte_len) {
            if (bit_len == 0) {
                // 刚好需要 byte_len*8 位，直接返回
                return idx_byte * 8;
            }
            
            // 还需要额外的 bit_len 位，检查后面连续为 0 的位
            uint32_t next_byte = idx_byte + byte_len;
            if (next_byte < btmp->btmp_bytes_len) {
                uint8_t mask = (1 << bit_len) - 1;  // 低 bit_len 位的掩码
                if ((btmp->bits[next_byte] & mask) == 0) {
                    return idx_byte * 8;
                }
            }
            
            // 后面不够，向前检查
            if (idx_byte > 0) {
                uint32_t prev_byte = idx_byte - 1;
                uint8_t mask = ((1 << bit_len) - 1) << (8 - bit_len);  // 高 bit_len 位的掩码
                if ((btmp->bits[prev_byte] & mask) == 0) {
                    return (prev_byte * 8 + (8 - bit_len));
                }
            }
            
            // 都不满足，继续找下一个位置
            idx_byte += byte_len + 1;           // 已经检查 byte_len 个字节,再加上下个字节一定不满足条件也跳过. (情况2同理)
            continue;
        }
        
        // 情况2：找到了 byte_len-1 个全0字节
        // 特殊情况: 10000000 00000000 00000001
        if (j == byte_len - 1 && byte_len > 0) {
            // 向两边扫描
            uint32_t need_bits = cnt - (byte_len - 1) * 8;  // 还需要的位数
            
            // 检查前面字节的高位
            if (idx_byte > 0) {
                uint32_t prev_byte = idx_byte - 1;
                // 计算前面字节有多少个高位是0
                uint8_t prev_zero_bits = 0;
                for (int8_t bit = 7; bit >= 0; bit--) {
                    if (!(btmp->bits[prev_byte] & (1 << bit))) {
                        prev_zero_bits++;
                    } else {
                        break;
                    }
                }
                
                if (prev_zero_bits >= need_bits) {
                    // 前面够用
                    return (prev_byte * 8 + (8 - need_bits));
                }
                
                // 前面不够，检查后面
                uint32_t next_byte = idx_byte + (byte_len - 1);
                if (next_byte < btmp->btmp_bytes_len) {
                    uint32_t remain_bits = need_bits - prev_zero_bits;
                    uint8_t mask = (1 << remain_bits) - 1;
                    if ((btmp->bits[next_byte] & mask) == 0) {
                        return (prev_byte * 8 + (8 - prev_zero_bits));
                    }
                }
            }
            
            // 如果前面没有字节，只检查后面
            if (idx_byte == 0) {
                uint32_t next_byte = idx_byte + (byte_len - 1);
                if (next_byte < btmp->btmp_bytes_len) {
                    uint8_t mask = (1 << need_bits) - 1;
                    if ((btmp->bits[next_byte] & mask) == 0) {
                        return idx_byte * 8;
                    }
                }
            }
            
            // 情况2不满足，跳过已检查的字节
            idx_byte += j + 1;  //已检查: bits[idx_byte-1]的高位, bits[idx_byte..idx_byte+j-1], bits[idx_byte+j]的低位
            continue;
        }
        
        // j < byte_len-1，说明找到的全0字节太少，跳过这些字节
        idx_byte += j + 1;          //同理 idx+j 不满足条件,第j+1个字节也不满足
    }
    
    return -1;
}