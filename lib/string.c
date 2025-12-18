#include "string.h"
#include "global.h"
#include "debug.h"

/* 将 dst_起始的 size 个字节置为 value */
void memset(void* dst_, uint8_t value, uint32_t size){
    ASSERT(dst_ != NULL);
    uint8_t* dst = (uint8_t*)dst_;
    while (size-- > 0)
        *dst++ = value;
}

/* 将 src_起始的 size 个字节复制到 dst_ */
void memcpy(void* dst_, const void* src_, uint32_t size){
    ASSERT(dst_ != NULL && src_ != NULL);
    uint8_t* dst = dst_;
    const uint8_t* src = src_;
    while (size-- > 0)
        *dst++ = *src++;

}

/* 连续比较以地址 a_和地址 b_开头的 size 个字节，若相等则返回 0，若 a_大于 b_，返回+1，否则返回−1 */
int memcmp(const void* a_, const void* b_, uint32_t size){
    const char* a = a_;
    const char* b = b_;
    ASSERT(a != NULL && b != NULL);
    while(size-- > 0){
        if(*a != *b){
            return *a > *b ? 1 : -1;
        }
        a++;
        b++;
    }
    return 0;
}

/* 将字符串从 src_复制到 dst_ */
char* strcpy(char* dst_, const char* src_){
    ASSERT(dst_ != NULL && src_ != NULL);
    char* r = dst_;
    while((*dst_++ = *src_++) != 0);
    return r;
}

/* 返回字符串长度 */
uint32_t strlen(const char* str){
    ASSERT(str != NULL);
    const char* p = str;
    while(*p++);
    return p - str - 1;
}

/* 比较字符串 a_和 b_，若相等则返回 0，若 a_大于 b_，返回+1，否则返回−1 */
int strcmp(const char* a_, const char* b_){
    ASSERT(a_ != NULL && b_ != NULL);
    while(*a_ && (*a_ == *b_)){
        a_++;
        b_++;
    }
    return *a_ > *b_ ? 1 : -1;
}

/* 从左到右查找字符串 str 中首次出现字符 ch 的地址*/
char* strchr(const char* str, const uint8_t ch){
    ASSERT(str != NULL);
    while(*str != 0){
        if(*str == ch){
            return (char*)str;
        }
        str++;
    }
    return NULL;
}

/* 从右到左查找字符串 str 中首次出现字符 ch 的地址 == 从左到右最后一次出现的位置 */
char* strrchr(const char* str, const uint8_t ch){
    ASSERT(str != NULL);
    const char* last_char = NULL;       //用来保存最后一次出现ch的地址
    while(*str != 0){
        if(*str == ch){
            last_char = str;
        }
        str++;
    }
    return (char*)last_char;
}

/* 从左到右查找字符串 str 中首次出现字符 ch 的位置下标，失败返回 -1 */
uint32_t strchrs(const char* str, const uint8_t ch){
    ASSERT(str != NULL);
    uint32_t index = 0;
    while(*str != 0){
        if(*str == ch){
            return index;
        }
        str++;
        index++;
    }
    return -1;
}

/* 从右到左查找字符串 str 中首次出现字符 ch 的位置下标，失败返回 -1 */
int strrchrs(const char* str, const uint8_t ch){
    ASSERT(str != NULL);
    int index = -1;
    int i = 0;
    while(*str != 0){
        if(*str == ch){
            index = i;  //更新index为最新出现ch的位置
        }
        str++;
        i++;
    }
    return index;
}

/* 将字符串 src_拼接到 dst_的结尾 */
char* strcat(char* dst_, const char* src_){
    ASSERT(dst_ != NULL && src_ != NULL);
    char* r = dst_;
    while(*dst_ != 0){
        dst_++;
    }
    while((*dst_++ = *src_++) != 0);
    return r;
}

/* 在字符串 str 中查找字符 ch 出现的次数 */
uint32_t strcount(const char* str, uint8_t ch){
    ASSERT(str != NULL);
    uint32_t ch_cnt = 0;
    const char* p = str;
    while(*p != 0){
        if(*p == ch) ch_cnt++;
        p++;
    }
    return ch_cnt;
}