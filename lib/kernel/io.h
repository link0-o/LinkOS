/****************** 机器模式 *******************
    b -- 输出寄存器 QImode 名称，即寄存器中的最低 8 位:[a-d]l
    w -- 输出寄存器 HImode 名称，即寄存器中 2 个字节的部分,如[a-d]x

    HImode
    "Half-Integer"模式，表示一个两字节的整数
    QImode
    "Quarter-Integer"模式，表示一个一字节的整数
******************************************************/
#ifndef  __LIB_IO_H
#define __LIB_IO_H
#include "stdint.h"
    /* 向端口 port 写入一个字节*/
    static inline void outb(uint16_t port, uint8_t data){
        //内联汇编
        //N表示0~255的一个立即数,a是(al,ah,eax),d同理
        asm volatile ("outb %b0,%w1" : : "a"(data),"Nd"(port));
    }
    
    /* 将 addr 处起始的 word_cnt 个字写入端口 port */
    static inline void outsw(uint16_t port, const void* addr, uint32_t word_cnt){
        /*outsw 是把 ds:esi 处的 16 位的内容写入 port 端口,我们在设置段描述符时,
        已经将 ds,es,ss 段的选择子都设置为相同的值了，此时不用担心数据错乱*/

        asm volatile ("cld; rep outsw" : "+S"(addr),"+c"(word_cnt) : "d"(port));
    }

    /* 将从端口 port 读入的一个字节返回 */
    static inline uint8_t inb(uint16_t port){
        uint8_t data;
        asm volatile("inb %w1,%b0" : "=a"(data) : "Nd"(port));
        return data;
    }

    /* 将从端口 port 读入的 word_cnt 个字写入 addr */
    static inline void insw(uint16_t port, void* addr, uint32_t word_cnt){
        asm volatile("cld; rep insw" : "+D"(addr), "+c"(word_cnt) : "d"(port) : "memory");
    }
    

#endif