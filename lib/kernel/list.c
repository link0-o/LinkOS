#include "list.h"
#include "interrupt.h"

/* 初始化双向链表 */
void list_init(List* plist) {
    plist->head.prev = NULL;
    plist->head.next = &plist->tail;
    plist->tail.prev = &plist->head;
    plist->tail.next = NULL;
}

/* 把链表元素 elem 插入在元素 before 之前 */
void list_insert_before(struct list_elem* before, struct list_elem* elem){
    enum intr_status old_status = intr_disable();           // 关闭中断, 并且返回 old 状态
    before->prev->next = elem;
    elem->prev = before->prev;
    elem->next = before;
    before->prev = elem;
    intr_set_status(old_status);                        // 设置为开始 old_status
}

/* 添加元素到列表队首 */
void list_push(List* plist, struct list_elem* elem){            //List -> struct list
    ist_insert_before(plist->head.next, elem);
}

/* 追加元素到链表队尾 */
void list_append(List* plist, struct list_elem* elem){
    list_insert_before(&plist->tail, elem);
}

/* 删除元素 pelem  */
void list_remove(struct list_elem* pelem){
    enum intr_status old_status = intr_disable();           // 关闭中断

    pelem->prev->next = pelem->next;
    pelem->next->prev = pelem->prev;

    intr_set_status(old_status);
}

/* 将链表第一个元素弹出并返回 */
struct list_elem* list_pop(List* plist){
    struct list_elem* elem = plist->head.next;
    list_remove(elem);
    return elem;
}

/* 从链表中查找元素 obj_elem，成功时返回 true，失败时返回 false */
bool elem_find(List* plist, struct list_elem* obj_elem){
    struct list_elem* elem = plist->head.next;
    while (elem != &plist->tail){
        if (elem == obj_elem){
            return true;
        }
        elem = elem->next;
    }
    return false;
}

/* 把列表 plist 中的每个元素 elem 和 arg 传给回调函数 func，arg 给 func 用来判断 elem 是否符合条件．*/
/* 本函数的功能是遍历列表内所有元素，逐个判断是否有符合条件的元素, 找到符合条件的元素返回元素指针，否则返回 NULL */
struct list_elem* list_traversal(List* plist, function func, int arg){
    struct list_elem* elem = plist->head.next;
    while (elem != &plist->tail){
        if (func(elem, arg)){
            return elem;
        }
        elem = elem->next;
    }
    return NULL;
}

/* 返回链表长度 */
uint32_t list_len(List* plist){
    struct list_elem* elem = plist->head.next;
    uint32_t length = 0;
    while (elem != &plist->tail){
        length++;
        elem = elem->next;
    }   
    return length;
}

/* 判断链表是否为空，空时返回 true，否则返回 false */
bool list_empty(List* plist){
    return (plist->head.next == &plist->tail ? true : false);
}