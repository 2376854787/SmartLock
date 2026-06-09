
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
 * @brief 作用域守卫：离开作用域时自动回滚未提交的事务。
 *
 * 用法：
 *   MP1_TXN_SCOPE(&txn) {
 *       p = mp1_txn_alloc(&txn);
 *       if (!p) break;          // break 安全：退出时会自动 abort
 *       ...
 *       mp1_txn_commit(&txn);   // 成功路径显式提交；提交后 abort 变 no-op
 *   }
 *
 * 语义：作用域退出时无条件调用 mp1_txn_abort()——
 *   - 已 commit：abort 因 active==false 直接返回，不误放任何块；
 *   - 未 commit：abort 回滚所有已分配块，杜绝泄漏。
 *
 * @note 与旧版不同：break 现在是安全的（旧实现 break 会跳过 for 的第三表达式
 *       导致不回滚、块泄漏）。这里用双层 for，内层承载 break，外层的清理在
 *       作用域结束时无条件执行，故 break / 正常退出都会回滚。
 * @warning 仍禁止在作用域内 return / goto 跳出——那会绕过守卫，造成泄漏。
 */
#define MP1_TXN_SCOPE(txn_ptr)                                                  \
    for (bool _mp1_done = false; !_mp1_done; mp1_txn_abort((txn_ptr)), _mp1_done = true) \
        for (bool _mp1_once = true; _mp1_once; _mp1_once = false)
#endif  // MEMPOOL_TXN_H
