#ifndef __KERNEL_DEBUG_H
#define __KERNEL_DEBUG_H

void panic_spin(char* filename, int line, const char* func, const char* condition);

/*************************** __VA_ARGS__ *******************************
 * __VA_ARGS__ 是预处理器所支持的专用标识符。
 * 代表所有与省略号相对应的参数。
 * 也就是...的内容会替换__VA_ARGS__   */

#define PANIC(...) panic_spin (__FILE__, __LINE__, __func__, __VA_ARGS__)

/***********************************************************************/
#ifdef NDEBUG           //release版本
    #define ASSERT(CONDITION) ((void)0)
#else
    #define ASSERT(CONDITION) \
    if(CONDITION){}\
    else{\
        /* 符号#让编译器将宏的参数转化为字符串字面量 */ \
        PANIC(#CONDITION); \
    }

#endif          /*__NDEBUG */

#endif          /*__KERNEL_DEBUG_H*/