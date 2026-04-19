#include "stdio.h"
#include "syscall.h"
#include "stdarg.h"
#include "string.h"

/* 把无符号整数转成字符串，直接写入 *buf 并后移指针
 * base: 进制，upper: 十六进制是否大写 */
static void uint_to_str(uint32_t value, char** buf, uint8_t base, uint8_t upper) {
    const char* digits_lower = "0123456789abcdef";
    const char* digits_upper = "0123456789ABCDEF";
    const char* digits = upper ? digits_upper : digits_lower;

    char tmp[32];
    int  len = 0;

    if (value == 0) {
        tmp[len++] = '0';
    } else {
        while (value) {
            tmp[len++] = digits[value % base];
            value /= base;
        }
    }

    /* 反转写入 buf */
    while (len > 0) {
        *(*buf)++ = tmp[--len];
    }
}

/* ─────────────────────────────────────────────
 * vsprintf：格式化写入 buf，返回写入字节数
 * ───────────────────────────────────────────── */
int vsprintf(char* buf, const char* fmt, va_list ap) {
    char* out = buf;

    while (*fmt) {
        if (*fmt != '%') {
            *out++ = *fmt++;
            continue;
        }

        fmt++;  /* 跳过 '%' */

        /* 解析标志位和宽度 */
        uint8_t  flag_zero  = 0;        // 补零
        uint8_t  flag_left  = 0;        // 左对齐
        int      width      = 0;

        /* 标志位 */
        while (*fmt == '0' || *fmt == '-') {
            if (*fmt == '0') flag_zero = 1;
            if (*fmt == '-') flag_left = 1;
            fmt++;
        }
        /* 宽度 */
        while (*fmt >= '1' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        if (flag_left) flag_zero = 0;

        char   tmp[64];
        char*  p   = tmp;
        int    neg = 0;

        switch (*fmt) {
            /* 十进制有符号 */
            case 'd': {
                int32_t v = va_arg(ap, int32_t);
                if (v < 0) { neg = 1; v = -v; }
                uint_to_str((uint32_t)v, &p, 10, 0);
                break;
            }
            /* 十进制无符号 */
            case 'u': {
                uint32_t v = va_arg(ap, uint32_t);
                uint_to_str(v, &p, 10, 0);
                break;
            }
            /* 十六进制小写 */
            case 'x': {
                uint32_t v = va_arg(ap, uint32_t);
                uint_to_str(v, &p, 16, 0);
                break;
            }
            /* 十六进制大写 */
            case 'X': {
                uint32_t v = va_arg(ap, uint32_t);
                uint_to_str(v, &p, 16, 1);
                break;
            }
            /* 八进制 */
            case 'o': {
                uint32_t v = va_arg(ap, uint32_t);
                uint_to_str(v, &p, 8, 0);
                break;
            }
            /* 字符 */
            case 'c': {
                char c = (char)va_arg(ap, int);
                int pad = width - 1;
                if (!flag_left) {
                    while (pad-- > 0) *out++ = ' ';
                }
                *out++ = c;
                if (flag_left) {
                    while (pad-- > 0) *out++ = ' ';
                }
                fmt++;
                continue;
            }
            /* 字符串 */
            case 's': {
                const char* s = va_arg(ap, const char*);
                if (!s) s = "(null)";
                int slen = 0;
                const char* ss = s;
                while (*ss++) slen++;

                int pad = width - slen;
                if (!flag_left) {
                    while (pad-- > 0) *out++ = ' ';
                }
                while (*s) *out++ = *s++;
                if (flag_left) {
                    while (pad-- > 0) *out++ = ' ';
                }
                fmt++;
                continue;
            }
            /* 字面 % */
            case '%': {
                *out++ = '%';
                fmt++;
                continue;
            }
            /* 未知说明符：原样输出 */
            default: {
                *out++ = '%';
                *out++ = *fmt;
                fmt++;
                continue;
            }
        }

        /* 写入数字串（含宽度/对齐/补零处理）*/
        int len = (int)(p - tmp);
        int total = len + neg;
        int pad = width - total;

        if (!flag_left) {
            if (flag_zero) {
                if (neg) *out++ = '-';
                while (pad-- > 0) *out++ = '0';
            } else {
                while (pad-- > 0) *out++ = ' ';
                if (neg) *out++ = '-';
            }
        } else {
            if (neg) *out++ = '-';
        }

        /* 写数字 */
        char* src = tmp;
        while (src < p) *out++ = *src++;

        /* 左对齐后补空格 */
        if (flag_left) {
            while (pad-- > 0) *out++ = ' ';
        }

        fmt++;
    }

    *out = '\0';
    return (int)(out - buf);
}

int sprintf(char* buf, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsprintf(buf, fmt, ap);
    va_end(ap);
    return n;
}

/* 格式化后通过 write(1,...) 输出到 stdout */
int printf(const char* fmt, ...) {
    char buf[1024];

    va_list ap;
    va_start(ap, fmt);
    int len = vsprintf(buf, fmt, ap);
    va_end(ap);

    return write(1, buf, (uint32_t)len);
}
