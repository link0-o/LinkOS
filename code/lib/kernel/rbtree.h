#ifndef __LIB_KERNEL_RBTREE_H
#define __LIB_KERNEL_RBTREE_H

#include "stdint.h"
#include "global.h"

/* 红黑树节点颜色 */
#define RB_RED      0
#define RB_BLACK    1

/* 红黑树节点结构 */
struct rb_node {
    struct rb_node* rb_parent;      // 父节点
    struct rb_node* rb_left;        // 左子节点
    struct rb_node* rb_right;       // 右子节点
    uint8_t rb_color;               // 节点颜色（红/黑）
};

/* 红黑树根结构 */
struct rb_root {
    struct rb_node* rb_node;        // 根节点指针
};

/* 初始化红黑树根 */
#define RB_ROOT { NULL }

/* 从 rb_node 获取包含它的结构体指针 */
#define rb_entry(ptr, type, member) \
    ((type*)((char*)(ptr) - (unsigned long)(&((type*)0)->member)))

/* 红黑树操作函数 */
void rb_insert_color(struct rb_node* node, struct rb_root* root);
void rb_erase(struct rb_node* node, struct rb_root* root);
struct rb_node* rb_first(struct rb_root* root);
struct rb_node* rb_last(struct rb_root* root);
struct rb_node* rb_next(struct rb_node* node);
struct rb_node* rb_prev(struct rb_node* node);

/* 辅助函数 只负责纯粹的插入(不一定保持红黑树性质) */
static inline void rb_link_node(struct rb_node* node, struct rb_node* parent, 
                                struct rb_node** rb_link) {
    node->rb_parent = parent;
    node->rb_color = RB_RED;
    node->rb_left = node->rb_right = NULL;
    *rb_link = node;
}

#endif
