#ifndef __LIB_KERNEL_LIST_H
#define __LIB_KERNEL_LIST_H
#include "global.h"

/* 双向链表 */

#define offset(struct_type,member) (int)(&((struct_type*)0)->member)        //计算结构体成员相对于结构体起始地址的偏移
#define elem2entry(struct_type, struct_member_name, elem_ptr) \             
    (struct_type*)((uint32_t)elem_ptr - offset(struct_type, struct_member_name))        //由成员元素的指针获取结构体指针

/* 定义链表结点成员结构, 结点中不需要数据成员，只要求前驱和后继结点指针 */
struct list_elem {
    struct list_elem* prev;  //前驱
    struct list_elem* next;  //后继
};

/* 链表结构，用来实现队列 */
typedef struct list {
    struct list_elem head;   // 头结点, 不存数据
    struct list_elem tail;   // 尾结点
} List;

/* 自定义函数类型 function，用于在 list_traversal 中做回调函数 */
typedef bool (function)(struct list_elem*, int arg);

void list_init (List* plist);
void list_insert_before(struct list_elem* before, struct list_elem* elem);      //在 before 结点前插入 elem 结点
void list_push(List* plist, struct list_elem* elem);             //在链表头部添加结点 elem
void list_iterate(List* plist);                 //遍历链表 plist 中的所有结点并打印
void list_append(List* plist, struct list_elem* elem);           //在链表尾部添加结点 elem
void list_remove(struct list_elem* pelem);              //从链表中删除结点 pelem
struct list_elem* list_pop(List* plist);        //弹出链表第一个结点并返回
bool list_empty(List* plist);                   //判断链表是否为空
uint32_t list_len(List* plist);                 //返回链表长度
struct list_elem* list_traversal(List* plist, function func, int arg);   //遍历链表，找到符合条件的结点并返回结点指针，没有则返回 NULL
bool elem_find(List* plist, struct list_elem* obj_elem);    //在链表中查找结点 obj_elem，找到返回 true，否则返回 false


#endif