#include "rbtree.h"
#include "stdint.h"
#include "global.h"

/* 获取节点颜色 */
static inline uint8_t rb_color(struct rb_node* node) {
    return node ? node->rb_color : RB_BLACK;
}

/* 设置节点颜色 */
static inline void rb_set_color(struct rb_node* node, uint8_t color) {
    if (node) node->rb_color = color;
}

/* 获取父节点 */
static inline struct rb_node* rb_parent(struct rb_node* node) {
    return node ? node->rb_parent : NULL;
}

/* 设置父节点 */
static inline void rb_set_parent(struct rb_node* node, struct rb_node* parent) {
    if (node) node->rb_parent = parent;
}

/* 左旋
 *     P               P
 *     |               |
 *     X               Y
 *    / \     =>      / \
 *   A   Y           X   C
 *      / \         / \
 *     B   C       A   B
 */
static void rb_rotate_left(struct rb_node* x, struct rb_root* root) {
    struct rb_node* y = x->rb_right;
    struct rb_node* parent = rb_parent(x);

    /* B 成为 X 的右子树 */
    x->rb_right = y->rb_left;
    if (y->rb_left) {
        rb_set_parent(y->rb_left, x);
    }

    /* Y 的父节点指向 X 的父节点 */
    rb_set_parent(y, parent);

    /* 更新父节点的子指针 */
    if (!parent) {
        root->rb_node = y;
    } else if (x == parent->rb_left) {
        parent->rb_left = y;
    } else {
        parent->rb_right = y;
    }

    /* X 成为 Y 的左子树 */
    y->rb_left = x;
    rb_set_parent(x, y);
}

/* 右旋
 *     P               P
 *     |               |
 *     Y               X
 *    / \     =>      / \
 *   X   C           A   Y
 *  / \                 / \
 * A   B               B   C
 */
static void rb_rotate_right(struct rb_node* y, struct rb_root* root) {
    struct rb_node* x = y->rb_left;
    struct rb_node* parent = rb_parent(y);

    /* B 成为 Y 的左子树 */
    y->rb_left = x->rb_right;
    if (x->rb_right) {
        rb_set_parent(x->rb_right, y);
    }

    /* X 的父节点指向 Y 的父节点 */
    rb_set_parent(x, parent);

    /* 更新父节点的子指针 */
    if (!parent) {
        root->rb_node = x;
    } else if (y == parent->rb_left) {
        parent->rb_left = x;
    } else {
        parent->rb_right = x;
    }

    /* Y 成为 X 的右子树 */
    x->rb_right = y;
    rb_set_parent(y, x);
}

/* 插入后修复红黑树性质 */
void rb_insert_color(struct rb_node* node, struct rb_root* root) {
    struct rb_node* parent;
    struct rb_node* gparent;

    /* 当父节点存在且为红色时需要调整 */
    while ((parent = rb_parent(node)) && rb_color(parent) == RB_RED) {
        gparent = rb_parent(parent);

        if (parent == gparent->rb_left) {
            /* 情况 1: 父节点是祖父节点的左子节点 */
            struct rb_node* uncle = gparent->rb_right;

            if (uncle && rb_color(uncle) == RB_RED) {
                /* Case 1: 叔叔节点是红色 */
                rb_set_color(parent, RB_BLACK);
                rb_set_color(uncle, RB_BLACK);
                rb_set_color(gparent, RB_RED);
                node = gparent;
                continue;
            }

            if (node == parent->rb_right) {
                /* Case 2: 当前节点是右子节点 属于 L - R 型 , 先左旋变成 L -L 再右旋 */
                struct rb_node* tmp;
                rb_rotate_left(parent, root);
                tmp = parent;
                parent = node;
                node = tmp;
            }

            /* Case 3: 当前节点是左子节点 */
            rb_set_color(parent, RB_BLACK);
            rb_set_color(gparent, RB_RED);
            rb_rotate_right(gparent, root);
        } else {
            /* 情况 2: 父节点是祖父节点的右子节点（镜像） */
            struct rb_node* uncle = gparent->rb_left;

            if (uncle && rb_color(uncle) == RB_RED) {
                /* Case 1: 叔叔节点是红色 */
                rb_set_color(parent, RB_BLACK);
                rb_set_color(uncle, RB_BLACK);
                rb_set_color(gparent, RB_RED);
                node = gparent;
                continue;
            }

            if (node == parent->rb_left) {
                /* Case 2: 当前节点是左子节点, 属于 R - L 型 , 先右旋变成 R - R 再左旋 */
                struct rb_node* tmp;
                rb_rotate_right(parent, root);
                tmp = parent;
                parent = node;
                node = tmp;
            }

            /* Case 3: 当前节点是右子节点 */
            rb_set_color(parent, RB_BLACK);
            rb_set_color(gparent, RB_RED);
            rb_rotate_left(gparent, root);
        }
    }

    /* 根节点必须是黑色 */
    rb_set_color(root->rb_node, RB_BLACK);
}

/* 删除后修复红黑树性质 */
static void rb_erase_color(struct rb_node* node, struct rb_node* parent,        // 第一个参数是child, 第二个是child的新父节点
                          struct rb_root* root) {
    struct rb_node* sibling;            // 兄弟节点

    // 从当前节点向上回溯，直到根节点
    while ((!node || rb_color(node) == RB_BLACK) && node != root->rb_node) {
        if (parent->rb_left == node) {
            sibling = parent->rb_right;

            if (rb_color(sibling) == RB_RED) {
                /* Case 1: 兄弟节点是红色 */
                rb_set_color(sibling, RB_BLACK);
                rb_set_color(parent, RB_RED);
                rb_rotate_left(parent, root);
                sibling = parent->rb_right;
            }

            if ((!sibling->rb_left || rb_color(sibling->rb_left) == RB_BLACK) &&
                (!sibling->rb_right || rb_color(sibling->rb_right) == RB_BLACK)) {
                /* Case 2: 兄弟节点的两个子节点都是黑色 -> 双重黑色节点上移(并且兄弟节点置为红色节点) */
                rb_set_color(sibling, RB_RED);
                node = parent;
                parent = rb_parent(node);
            } else {
                if (!sibling->rb_right || rb_color(sibling->rb_right) == RB_BLACK) {
                    /* Case 3: 兄弟节点的右子节点是黑色 -> 先右旋转sibling再左旋parent */
                    rb_set_color(sibling->rb_left, RB_BLACK);
                    rb_set_color(sibling, RB_RED);
                    rb_rotate_right(sibling, root);
                    sibling = parent->rb_right;
                }

                /* Case 4: 兄弟节点的右子节点是红色 */
                rb_set_color(sibling, rb_color(parent));
                rb_set_color(parent, RB_BLACK);
                rb_set_color(sibling->rb_right, RB_BLACK);
                rb_rotate_left(parent, root);
                node = root->rb_node;
                break;
            }
        } else {
            /* 镜像情况 */
            sibling = parent->rb_left;

            if (rb_color(sibling) == RB_RED) {
                rb_set_color(sibling, RB_BLACK);
                rb_set_color(parent, RB_RED);
                rb_rotate_right(parent, root);
                sibling = parent->rb_left;
            }

            if ((!sibling->rb_left || rb_color(sibling->rb_left) == RB_BLACK) &&
                (!sibling->rb_right || rb_color(sibling->rb_right) == RB_BLACK)) {
                rb_set_color(sibling, RB_RED);
                node = parent;
                parent = rb_parent(node);
            } else {
                if (!sibling->rb_left || rb_color(sibling->rb_left) == RB_BLACK) {
                    rb_set_color(sibling->rb_right, RB_BLACK);
                    rb_set_color(sibling, RB_RED);
                    rb_rotate_left(sibling, root);
                    sibling = parent->rb_left;
                }

                rb_set_color(sibling, rb_color(parent));
                rb_set_color(parent, RB_BLACK);
                rb_set_color(sibling->rb_left, RB_BLACK);
                rb_rotate_right(parent, root);
                node = root->rb_node;
                break;
            }
        }
    }

    if (node) rb_set_color(node, RB_BLACK);
}

/* 删除节点 */
void rb_erase(struct rb_node* node, struct rb_root* root) {
    struct rb_node* child = NULL, *parent = NULL;
    uint8_t color;

    if (!node->rb_left) {
        child = node->rb_right;
    } else if (!node->rb_right) {
        child = node->rb_left;
    } else {
        /* 有两个子节点，找后继节点 */
        // 找到右子树的最左节点(最小的节点)
        struct rb_node* successor = node->rb_right;
        while (successor->rb_left) {
            successor = successor->rb_left;
        }

        /* 用后继节点替换当前节点 */
        if (rb_parent(node)) {
            if (rb_parent(node)->rb_left == node) {
                rb_parent(node)->rb_left = successor;
            } else {
                rb_parent(node)->rb_right = successor;
            }
        } else {                // 该节点是根节点
            root->rb_node = successor;
        }

        child = successor->rb_right;
        parent = rb_parent(successor);
        color = rb_color(successor);


        //将后继节点的右子树连接到其父节点
        if (parent == node) {
            parent = successor;
        } else {
            if (child) rb_set_parent(child, parent);
            parent->rb_left = child;
            successor->rb_right = node->rb_right;
            rb_set_parent(node->rb_right, successor);
        }

        successor->rb_parent = node->rb_parent;
        successor->rb_color = node->rb_color;
        successor->rb_left = node->rb_left;
        rb_set_parent(node->rb_left, successor);

        if (color == RB_BLACK) {                // successor原来的颜色
            rb_erase_color(child, parent, root);
        }
        return;
    }

    parent = rb_parent(node);
    color = rb_color(node);

    if (child) rb_set_parent(child, parent);

    if (parent) {
        if (parent->rb_left == node) {
            parent->rb_left = child;
        } else {
            parent->rb_right = child;
        }
    } else {
        root->rb_node = child;
    }

    if (color == RB_BLACK) {
        rb_erase_color(child, parent, root);
    }
}

/* 找到最左节点（最小值） */
struct rb_node* rb_first(struct rb_root* root) {
    struct rb_node* node = root->rb_node;
    if (!node) return NULL;
    while (node->rb_left) {
        node = node->rb_left;
    }
    return node;
}

/* 找到最右节点（最大值） */
struct rb_node* rb_last(struct rb_root* root) {
    struct rb_node* node = root->rb_node;
    if (!node) return NULL;
    while (node->rb_right) {
        node = node->rb_right;
    }
    return node;
}

/* 找到下一个节点（中序遍历） */
struct rb_node* rb_next(struct rb_node* node) {
    if (!node) return NULL;

    /* 如果有右子树，返回右子树的最左节点 */
    if (node->rb_right) {
        node = node->rb_right;
        while (node->rb_left) {
            node = node->rb_left;
        }
        return node;
    }

    /* 否则向上找第一个作为左子节点的祖先 */
    struct rb_node* parent;
    while ((parent = rb_parent(node)) && node == parent->rb_right) {            // node不是他父亲的右子树
        node = parent;
    }
    return parent;
}

/* 找到前一个节点（中序遍历） */
struct rb_node* rb_prev(struct rb_node* node) {
    if (!node) return NULL;

    /* 如果有左子树，返回左子树的最右节点 */
    if (node->rb_left) {
        node = node->rb_left;
        while (node->rb_right) {
            node = node->rb_right;
        }
        return node;
    }

    /* 否则向上找第一个作为右子节点的祖先 */
    struct rb_node* parent;
    while ((parent = rb_parent(node)) && node == parent->rb_left) {
        node = parent;
    }
    return parent;
}
