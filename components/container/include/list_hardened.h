#ifndef SMARTLOCK_LIST_H
#define SMARTLOCK_LIST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "assert_cus.h"
#include "complier_cus.h"
#include "utils_def.h"

/*　========================   配置   ========================== */
/* * 注意：使用 #if 判断时，未定义的宏会被视为 0。
 * 因此这里确保定义为 0 或 1 即可。
 */
#ifndef LIST_CFG_DEBUG
#define LIST_CFG_DEBUG 1 /*　0:关闭校验 1:开启校验 */
#endif

#ifndef LIST_CFG_POISON
#define LIST_CFG_POISON 1
#endif

/* 断言 */
#ifndef LIST_ASSERT
#define LIST_ASSERT(x) ASSERT_FATAL((x))
#endif

/* =============================   基础工具    ========================== */
#define list_entry(ptr, type, member) container_of((ptr), type, member)

/* 投毒防止预防奇怪BUG */
#if LIST_CFG_POISON
#define LIST_POISON1 ((uintptr_t)0xDEAD0001u)
#define LIST_POISON2 ((uintptr_t)0xDEAD0002u)
#endif

/* 节点状态 */
typedef enum {
    LIST_NODE_UNLINKED = 0,
    LIST_NODE_LINKED   = 1,
} list_node_state_t;

/* 节点信息 */
typedef struct list_node {
    struct list_node *next;
    struct list_node *prev;
#if LIST_CFG_DEBUG
    uint32_t guard;  /* 完整性校验 地址绑定 */
    uint8_t state;   /* 防止重复链接释放 */
    uint8_t _rsv[3]; /* 保留字节 用于4字节对齐 */
#endif
} list_node_t;

typedef list_node_t list_head_t;

/**
 * @brief 利用魔术字 + 节点信息 XOR 生成校验信息 防止内存破坏
 */
CORE_INLINE uint32_t list_calc_guard(const list_node_t *node) {
    const uintptr_t a = (uintptr_t)node;
    return (uint32_t)(0xC0FFEE11u ^ (uint32_t)a ^ (uint32_t)(a >> 16));
}

/**
 * @brief 结点初始化
 */
CORE_INLINE void list_node_init(list_node_t *node) {
    /* 初始状态自己指向自己 */
    node->prev = node;
    node->next = node;
#if LIST_CFG_DEBUG
    node->guard = list_calc_guard(node);
    node->state = LIST_NODE_UNLINKED;
#endif
}

/**
 * @brief 初始化头结点
 */
CORE_INLINE void list_head_init(list_head_t *head) {
    list_node_init(head);
#if LIST_CFG_DEBUG
    /* 头结点永远被视为链接在链表上 */
    head->state = LIST_NODE_LINKED;
#endif
}

/**
 * @brief 判断当前节点是否为空
 */
CORE_INLINE bool list_is_empty(const list_head_t *head) {
    return (head->next == head);
}

#if LIST_CFG_DEBUG
/**
 * @brief 检查单个节点是否合法（非空、Guard 校验通过）
 * @param node 结点句柄
 */
CORE_INLINE void list_check_node(const list_node_t *node) {
    LIST_ASSERT(node != NULL);
    /* 检查内存地址是否被篡改 */
    LIST_ASSERT(node->guard == list_calc_guard(node));
}
/**
 * @brief 检查两个相邻节点之间的连接是否紧密、正确。
 * @param prev 前结点
 * @param next 后结点
 */
CORE_INLINE void list_check_link(const list_node_t *prev, const list_node_t *next) {
    LIST_ASSERT(prev->next == next);
    LIST_ASSERT(next->prev == prev);
}
/**
 * @brief 校验头结点 包括非空、Guard 校验、 与父结点、子结点的连接性
 * @param head 头结点
 */
CORE_INLINE void list_check_head(const list_head_t *head) {
    /* 1、非空、Guard 校验 */
    list_check_node(head);
    /* 2、链接状态校验 */
    LIST_ASSERT(head->state == LIST_NODE_LINKED);
    /* 3、子结点、父结点非空、Guard 校验 */
    list_check_node(head->next);
    list_check_node(head->prev);
    /* 4、子结点、父结点 与头结点的连接性校验 */
    list_check_link(head, head->next);
    list_check_link(head->prev, head);
}
#else
#define list_check_node(node)       ((void)0)
#define list_check_link(prev, next) ((void)0)
#define list_check_head(head)       ((void)0)
#endif
/**
 * @brief 将结点插入 到目标 父 子结点的中间
 * @param node 插入结点句柄
 * @param prev 目标父结点
 * @param next 目标子节点
 */
CORE_INLINE void __list_add(list_node_t *node, list_node_t *prev, list_node_t *next) {
#if LIST_CFG_DEBUG
    /* DEBUG模式 校验结点的安全有效 */
    list_check_node(node);
    list_check_node(prev);
    list_check_node(next);
    LIST_ASSERT(node->state == LIST_NODE_UNLINKED);
    list_check_link(prev, next);
#endif
    /* 将node 插入到 prev - node  - next 中间 */
    next->prev = node;
    node->next = next;
    node->prev = prev;
    prev->next = node;
#if LIST_CFG_DEBUG
    /* DEBUG模式 插入完成后更新状态 检查双向链表的连接性 */
    node->state = LIST_NODE_LINKED;
    list_check_link(prev, node);
    list_check_link(node, next);
#endif
}
/**
 * @brief 将一个结点插入到链表的头部 头插法
 * @param node 插入结点
 * @param head 被插入的链表
 */
CORE_INLINE void list_add(list_node_t *node, list_head_t *head) {
    list_check_head(head);
    __list_add(node, head, head->next);
}
/**
 * @brief 尾插法
 * @param node 结点句柄
 * @param head 头结点
 */
CORE_INLINE void list_add_tail(list_node_t *node, list_head_t *head) {
    list_check_head(head);
    __list_add(node, head->prev, head);
}
/**
 * @brief 让 prev 和 next 互联，架空中间的那个结点
 * @param prev 父结点
 * @param next 子结点
 */
CORE_INLINE void __list_del(list_node_t *prev, list_node_t *next) {
#if LIST_CFG_DEBUG
    /* DEBUG模式 校验结点的安全有效 */
    list_check_node(prev);
    list_check_node(next);
    list_check_link(prev, next);
#endif
    /* 父子互联 */
    next->prev = prev;
    prev->next = next;
}

CORE_INLINE void list_del(list_node_t *node) {
#if LIST_CFG_DEBUG
    /* DEBUG模式 校验结点的安全有效 */
    list_check_node(node);
    LIST_ASSERT(node->state == LIST_NODE_LINKED);
    list_check_node(node->prev);
    list_check_node(node->next);
    list_check_link(node->prev, node);
    list_check_link(node, node->next);
#endif
    __list_del(node->prev, node->next);
#if LIST_CFG_POISON
    /* 投毒 */
    node->next = (list_head_t *)LIST_POISON1;
    node->prev = (list_head_t *)LIST_POISON2;
#else
    node->prev = node;
    node->next = node;
#endif
#if LIST_CFG_DEBUG
    /* 更新结点链接状态 */
    node->state = LIST_NODE_UNLINKED;
#endif
}
/**
 * @brief 新结点替换旧结点在链表中的链接位置
 * @param old 旧结点
 * @param new 新结点
 */
CORE_INLINE void list_replace(list_node_t *old, list_node_t *new) {
#if LIST_CFG_DEBUG
    /* DEBUG模式 校验结点的安全有效 */
    list_check_node(old);
    list_check_node(new);
    /* DEBUG模式 校验结点的状态有效 */
    LIST_ASSERT(old->state == LIST_NODE_LINKED);
    LIST_ASSERT(new->state == LIST_NODE_UNLINKED);
#endif
    /* 替换结点 */
    new->next       = old->next;
    new->prev       = old->prev;
    old->prev->next = new;
    old->next->prev = new;
#if LIST_CFG_POISON
    /* 给旧结点 投毒 */
    old->next = (list_head_t *)LIST_POISON1;
    old->prev = (list_head_t *)LIST_POISON2;
#endif
#if LIST_CFG_DEBUG
    /* 更新两个结点的状态 */
    new->state = LIST_NODE_LINKED;
    old->state = LIST_NODE_UNLINKED;
#endif
}
/**
 * @brief 将结点从原链表删除 加入新链表 头插法
 * @param node 结点
 * @param head 目标链表
 */
CORE_INLINE void list_move(list_node_t *node, list_head_t *head) {
    list_del(node);
    list_add(node, head);
}
/**
 * @brief 将结点从原链表删除 加入新链表 尾插法
 * @param node 结点
 * @param head 目标链表
 */
CORE_INLINE void list_move_tail(list_node_t *node, list_head_t *head) {
    list_del(node);
    list_add_tail(node, head);
}
/**
 * @brief 拼接两个链表
 * @param list 原链表
 * @param head 目标新链表
 */
CORE_INLINE void list_splice_init(list_head_t *list, list_head_t *head) {
    /* DEBUG模式 校验两个链表头的安全有效 */
    list_check_head(list);
    list_check_head(head);
    if (list_is_empty(list)) {
        return;
    }
    list_node_t *first = list->next;
    list_node_t *last  = list->prev;
    list_node_t *at    = head->next;

    at->prev           = last;
    last->next         = at;
    first->prev        = head;
    head->next         = first;
    /* 清空list */
    list_head_init(list);
}

/**
 * @brief 遍历链表返回当前遍历位置的结点地址
 * @param pos 结点指针
 * @param head 链表头结点
 */
#define list_for_each(pos, head) for ((pos) = (head)->next; (pos) != (head); (pos) = (pos)->next)
/**
 *
 * @param pos 结点指针保存当前遍历到的结点指针
 * @param n   提前保留下一个结点的地址 防止断链
 * @param head 遍历目标链表
 */
#define list_for_each_safe(pos, n, head) \
    for ((pos) = (head)->next, (n) = (pos)->next; (pos) != (head); (pos) = (n), (n) = (pos)->next)
/**
 *
 * @param pos 存储宿主结构体的基地址
 * @param head 目标链表
 * @param member 结构体成员
 * @param type
 */
#define list_for_each_entry(pos, head, member, type)                                 \
    for ((pos) = list_entry((head)->next, type, member); &((pos)->member) != (head); \
         (pos) = list_entry((pos)->member.next, type, member))

#define list_for_each_entry_safe(pos, tmp, head, member, type) \
    for ((pos) = list_entry((head)->next, type, member),       \
        (tmp)  = list_entry((pos)->member.next, type, member); \
         &((pos)->member) != (head);                           \
         (pos) = (tmp), (tmp) = list_entry((tmp)->member.next, type, member))

#endif  // SMARTLOCK_LIST_H