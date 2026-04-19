#ifndef __LIB_STDARG_H
#define __LIB_STDARG_H

/* 可变参数宏实现（基于 GCC 内建支持）
 * 
 * 原理：
 * 在 x86 调用约定中，参数从右到左压栈
 * 例如：func(a, b, c, ...) 的栈布局：
 * 
 *   高地址
 *   +----------------+
 *   | 可变参数 ...   |  ← va_arg 逐个取出
 *   +----------------+
 *   | 参数 c         |
 *   +----------------+
 *   | 参数 b         |
 *   +----------------+
 *   | 参数 a         |  ← last_arg
 *   +----------------+
 *   | 返回地址       |
 *   +----------------+  ← ebp
 *   低地址
 * 
 * va_list: 指向可变参数的指针
 * va_start: 让 va_list 指向第一个可变参数（last_arg 后面）
 * va_arg: 取出当前参数，并移动指针到下一个
 * va_end: 清理（在我们的简化实现中不需要做什么）
 */

typedef char* va_list;

/* 对齐到 4 字节边界（x86 32位栈对齐）
 * 例如：char 占 1 字节，但在栈上占 4 字节 */
#define _INTSIZEOF(n) ((sizeof(n) + sizeof(int) - 1) & ~(sizeof(int) - 1))

/* 初始化 va_list，让它指向第一个可变参数
 * last_arg: 最后一个固定参数的名字 */
#define va_start(ap, last_arg) \
    (ap = (va_list)&(last_arg) + _INTSIZEOF(last_arg))

/* 取出当前参数，并移动到下一个
 * type: 参数的类型 */
#define va_arg(ap, type) \
    (*(type*)((ap += _INTSIZEOF(type)) - _INTSIZEOF(type)))

/* 清理 va_list（我们的简化版本不需要做什么）*/
#define va_end(ap) (ap = (va_list)0)

#endif /* __LIB_STDARG_H */
