#ifndef _LIB_STRING_H
#define _LIB_STRING_H

#include "stdint.h"

void memset(void* dst_, uint8_t value, uint32_t size);
void memcpy(void* dst_, const void* src_, uint32_t size);
int memcmp(const void* a_, const void* b_, uint32_t size);
char* strcpy(char* dst_, const char* src_);
uint32_t strlen(const char* str);
int strcmp(const char* a_, const char* b_);
char* strchr(const char* str, const uint8_t ch);
char* strrchr(const char* str, const uint8_t ch);
uint32_t strchrs(const char* str, const uint8_t ch);
int strrchrs(const char* str, const uint8_t ch);
char* strcat(char* dst_, const char* src_);
uint32_t strcount(const char* str, uint8_t ch);

#endif
