
#ifndef MEMPOOL_TXN_H
#define MEMPOOL_TXN_H
#include <stddef.h>

#include "memory_pool1.h"
typedef struct {
    mp_pool1_t *pool;
    void **track;
    uint16_t cap;
    uint16_t used;
    bool active;
} mp1_txn_t;

/**
 * @brief 事物封装 初始化
 * @param t 事物句柄
 * @param pool 内存池句柄
 * @param track 保存申请了的内存块的负载基地址
 * @param cap 事物的容量
 */
CORE_INLINE void mp1_txn_begin(mp1_txn_t *t, mp_pool1_t *pool, void **track, uint16_t cap) {
    t->pool   = pool;
    t->track  = track;
    t->cap    = cap;
    t->used   = 0;
    t->active = true;
}
/**
 * @brief 从内存池申请一个块 并将其记录在事物 管理容器
 * @param t 内存池事物
 * @return 块的有效负载基地址
 */
CORE_INLINE void *mp1_txn_alloc(mp1_txn_t *t) {
    /* 必须有容器存储 获取的块的地址 */
    if (!t || !t->track) {
        return NULL;
    }
    /* 已申请的不能大于 内存池的容量 */
    if (t->used >= t->cap) {
        return NULL;
    }
    /* 获取块地址 */
    void *p = mp_alloc(t->pool);
    if (!p) {
        return NULL;
    }
    /* 将块地址存储一份 */
    t->track[t->used++] = p;
    return p;
}

/**
 * @brief 等待所需要的空间都分配完成执行
 * @param t 事物 句柄
 */
CORE_INLINE void mp1_txn_commit(mp1_txn_t *t) {
    if (!t) return;
    t->used   = 0;
    t->active = false;
}
/**
 * @brief 释放掉未提交的事物的 记录块
 * @param t 事物 句柄
 */
CORE_INLINE void mp1_txn_abort(mp1_txn_t *t) {
    if (!t || !t->active) return;
    while (t->used > 0) {
        void *p = t->track[--t->used];
        (void)mp_free(t->pool, p);
    }
    t->active = false;
}
/**
 * @brief 将没有提交成功的事物记录的已分配的块全部释放
 * @param txn_ptr 事物句柄
 * @note 只能使用 break 不能内部使用 return
 */
#define MP1_TXN_SCOPE(txn_ptr) \
    for (bool _once = true; _once; mp1_txn_abort((txn_ptr)), _once = false)
#endif  // MEMPOOL_TXN_H
