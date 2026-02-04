#include "APP_config.h"
#if defined(ENABLE_MEMORY_POOL)
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>

#include "compiler_cus.h"
#include "memory_pool.h"
#define RET_MEM_CODE(_cla, _rea) \
    RET_MAKE(RET_MOD_MEM, RET_SUB_MEM_POOL, RET_CODE_MAKE((_cla), (_rea)))
/**
 * @brief 返回指定块是否已分配
 * @param bm 位图
 * @param id 块id
 * @return 返回指定块是否已分配
 */
CORE_INLINE bool bm_test(const uint32_t *bm, uint16_t id) {
    return (bm[id >> 5] >> (id & 31)) & 1u;
}
/**
 * @brief 将指定块标记为已分配
 * @param bm 位图
 * @param id 块id
 */
CORE_INLINE void bm_set(uint32_t *bm, uint16_t id) {
    bm[id >> 5] |= 1u << (id & 31);
}
/**
 * @brief 将指定块标记为已释放
 * @param bm 位图
 * @param id 块id
 */
CORE_INLINE void bm_clear(uint32_t *bm, uint16_t id) {
    bm[id >> 5] &= ~(1u << (id & 31));
}

/**
 * @brief 查询指定块的基地址
 * @param p 内存池句柄
 * @param id 块id
 * @return 返回该块的基地址
 */
CORE_INLINE uint8_t *blk_ptr(mp_pool_t *p, uint16_t id) {
    return p->base + (uint32_t)id * p->blk_total_size;
}
/**
 * brief 根据块基地址返回有效负载的基地址
 * @param p 内存池句柄
 * @param blk 块的基地址
 * @return 返回有效负载的基地址
 */
CORE_INLINE void *blk_payload(mp_pool_t *p, uint8_t *blk) {
#if MP_CFG_CANARY
    return (void *)(blk + sizeof(uint32_t));
#else
    return (void *)blk;
#endif
}
/**
 * @brief 根据负载基地址计算块基地址
 * @param p 内存池句柄
 * @param payload 负载的基地址
 * @return 块的基地址
 */
CORE_INLINE uint8_t *payload_to_blk(mp_pool_t *p, void *payload) {
#if MP_CFG_CANARY
    return ((uint8_t *)payload) - sizeof(uint32_t);
#else
    return (uint8_t *)payload;
#endif
}
/**
 * @brief 给块的头尾写入魔数字
 * @param p 内存池句柄
 * @param blk 块的基地址
 */
CORE_INLINE void write_canary(mp_pool_t *p, uint8_t *blk) {
#if MP_CFG_CANARY
    *(uint32_t *)(void *)(blk)                                          = p->canary_head;
    *(uint32_t *)(void *)(blk + sizeof(uint32_t) + p->blk_payload_size) = p->canary_tail;
#else
    (void)p;
    (void)blk;
#endif
}
/**
 * @brief 检查头尾区域是否被破坏
 * @param p 内存池句柄
 * @param blk 块基地址
 * @return 头尾区域是否被破坏
 */
static inline ret_code_t check_canary(mp_pool_t *p, uint8_t *blk) {
#if MP_CFG_CANARY
    const uint32_t h = *(uint32_t *)(void *)(blk);
    const uint32_t t = *(uint32_t *)(void *)(blk + sizeof(uint32_t) + p->blk_payload_size);
    if (h != p->canary_head || t != p->canary_tail)
        return RET_MEM_CODE(RET_CLASS_DATA, RET_R_CHECKSUM);
#else
    (void)p;
    (void)blk;
#endif
    return RET_OK;
}

#if MP_CFG_QUARANTINE
/**
 * @brief 隔离队列是否满了
 * @param p 内存池句柄
 * @return 隔离间是否已满
 */
CORE_INLINE bool q_full(mp_pool_t *p) {
    return p->q_cnt == p->q_cap;
}
/**
 *
 * @param p 内存池句柄
 * @return
 */
CORE_INLINE bool q_empty(mp_pool_t *p) {
    return p->q_cnt == 0;
}
/**
 * @brief 将指定块存入队隔离间
 * @param p 内存池句柄
 * @param id 块id
 */
CORE_INLINE void q_push(mp_pool_t *p, uint16_t id) {
    if (p->q_cap == 0) {
        p->free_stack[p->top++] = id;
        return;
    }
    if (q_full(p)) {
        const uint16_t old = p->q_ring[p->q_r];
        /* 读指针 +1 */
        p->q_r             = (uint16_t)((p->q_r + 1) % p->q_cap);
        p->q_cnt--;
        /* 入栈空闲队列 */
        p->free_stack[p->top++] = old;
    }
    /* 写入新的块id */
    p->q_ring[p->q_w] = id;
    /* 写指针更新指向下一个可写入的索引位置 */
    p->q_w            = (uint16_t)((p->q_w + 1u) % p->q_cap);
    /* 总量 + 1 */
    p->q_cnt++;
}
#endif

#endif
