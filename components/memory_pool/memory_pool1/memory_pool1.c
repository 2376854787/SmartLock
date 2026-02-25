/**
 * @file memory_pool1.c
 * @brief 内存池实现代码
 * @details 细节
 * @author yan
 * @version v1.0
 * @date 2026年-2月-25日
 * @copyright 版权
 */
#include "APP_config.h"
#if (defined(CFG_FEAT_MEMORY_POOL) && (CFG_FEAT_MEMORY_POOL == 1))
#include <stdbool.h>
#include <string.h>

#include "assert_cus.h"
#include "compiler_cus.h"
#include "memory_pool1.h"
#include "osal.h"
#define RET_MEM_CODE(_cla, _rea) \
    RET_MAKE(RET_MOD_MEM, RET_SUB_MEM_POOL, RET_CODE_MAKE((_cla), (_rea)))
#define MP_ALIGN_UP(size, align) (((size) + (align) - 1) & ~((align) - 1))

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
CORE_INLINE uint8_t *blk_ptr(const mp_pool1_t *p, uint16_t id) {
    return p->base + (uint32_t)id * p->blk_total_size;
}
/**
 * brief 根据块基地址返回有效负载的基地址
 * @param p 内存池句柄
 * @param blk 块的基地址
 * @return 返回有效负载的基地址
 */
CORE_INLINE void *blk_payload(const mp_pool1_t *p, uint8_t *blk) {
#if MP_CFG_CANARY
    return (void *)(blk + p->blk_head_offset);
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
CORE_INLINE uint8_t *payload_to_blk(const mp_pool1_t *p, void *payload) {
#if MP_CFG_CANARY
    return ((uint8_t *)payload) - p->blk_head_offset;
#else
    return (uint8_t *)payload;
#endif
}
/**
 * @brief 用于 DMA传输更新内存数据后 cpu读取数据调用
 * @param p 内存池句柄
 * @param payload 负载基地址
 * @note invalidate 函数需要自己实现
 */
void mp_dma_sync_for_cpu(const mp_pool1_t *p, void *payload) {
    ASSERT_PARAM((p != NULL) && (payload != NULL));
    if ((p == NULL) || (payload == NULL)) return;
    if (p->cache_ops.invalidate) {
        uint8_t *blk = payload_to_blk(p, payload);
        p->cache_ops.invalidate(blk, p->blk_total_size);
    }
}

/**
 * @brief 用于cpu 写完后 DMA读取前调用
 * @param p 内存池基地址
 * @param payload 负载基地址
 * @note clean函数需要自己实现
 */
void mp_dma_sync_for_device(const mp_pool1_t *p, void *payload) {
    ASSERT_PARAM((p != NULL) && (payload != NULL));
    if ((p == NULL) || (payload == NULL)) return;
    if (p->cache_ops.clean) {
        uint8_t *blk = payload_to_blk(p, payload);
        p->cache_ops.clean(blk, p->blk_total_size);
    }
}
/**
 * @brief 给块的头尾写入魔数字
 * @param p 内存池句柄
 * @param blk 块的基地址
 */
CORE_INLINE void write_canary(const mp_pool1_t *p, uint8_t *blk) {
#if MP_CFG_CANARY
    *(uint32_t *)(void *)(blk)                                            = p->canary_head;
    *(uint32_t *)(void *)(blk + p->blk_head_offset + p->blk_payload_size) = p->canary_tail;
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
static inline ret_code_t check_canary(const mp_pool1_t *p, const uint8_t *blk) {
#if MP_CFG_CANARY
    const uint32_t h = *(uint32_t *)(void *)(blk);
    const uint32_t t = *(uint32_t *)(void *)(blk + p->blk_head_offset + p->blk_payload_size);
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
CORE_INLINE bool q_full(const mp_pool1_t *p) {
    return p->q_cnt == p->q_cap;
}
/**
 *
 * @param p 内存池句柄
 * @return
 */
CORE_INLINE bool q_empty(const mp_pool1_t *p) {
    return p->q_cnt == 0;
}
/**
 * @brief 将指定块存入队隔离间
 * @param p 内存池句柄
 * @param id 块id
 */
CORE_INLINE void q_push(mp_pool1_t *p, uint16_t id) {
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
/**
 * @brief 将隔离间一块移动到可用内存池
 * @param p 内存池句柄
 * @return 是否将隔离间一块移动到可用内存池
 */
CORE_INLINE bool q_pop_one_to_freelist(mp_pool1_t *p) {
    if (q_empty(p)) {
        return false;
    }
    const uint16_t id = p->q_ring[p->q_r];
    p->q_r            = (uint16_t)((p->q_r + 1) % p->q_cap);
    p->q_cnt--;
    p->free_stack[p->top++] = id;
    return true;
}
#endif
/**
 * @brief 初始化内存池
 * @param p 内存池句柄
 * @param cfg
 */
ret_code_t mp_init(mp_pool1_t *p, const mp_config_t *cfg) {
    /* 1. 基础指针校验 */
    ASSERT_PARAM((p != NULL) && (cfg != NULL) && (cfg->pool_mem != NULL) &&
                 (cfg->free_stack != NULL) && (cfg->alloc_bm != NULL));
    REQUIRE_RET((p != NULL) && (cfg != NULL) && (cfg->pool_mem != NULL) &&
                    (cfg->free_stack != NULL) && (cfg->alloc_bm != NULL),
                RET_MEM_CODE(RET_CLASS_PARAM, RET_R_NULL_PTR));
#if MP_CFG_QUARANTINE
    /* 隔离队列地址错误 但是隔离间数量大于 0*/
    REQUIRE_RET((cfg->quarantine_cap == 0u) || (cfg->quarantine_buf != NULL),
                RET_MEM_CODE(RET_CLASS_PARAM, RET_R_NULL_PTR));
#endif
    REQUIRE_RET((cfg->n_blks != 0u) && (cfg->payload_size != 0u) && (cfg->pool_size != 0u),
                RET_MEM_CODE(RET_CLASS_PARAM, RET_R_INVALID_ARG));
    /* 内存池基地址 */
    const uintptr_t raw_addr     = (uintptr_t)cfg->pool_mem;
    const uintptr_t aligned_addr = MP_ALIGN_UP(raw_addr, MP_CACHE_LINE_SIZE);
    /* 对齐基地址，浪费头部的空间 */
    const uint32_t lost_at_base  = (uint32_t)(aligned_addr - raw_addr);

    /* 对齐后的基地址是不是超出总容量 */
    if (lost_at_base >= cfg->pool_size) {
        return RET_MEM_CODE(RET_CLASS_RESOURCE, RET_R_NO_MEM);  // 内存太小，连对齐都不够
    }
    /* 计算对齐后剩余的有效总字节数 */
    const uint32_t available_bytes = cfg->pool_size - lost_at_base;
    /* 单块 大小 */
    uint16_t final_head_offset     = 0;
    uint32_t content_size          = 0;

#if MP_CFG_CANARY
    const uint16_t raw_head_size        = sizeof(uint32_t);
    const uint16_t raw_tail_size        = sizeof(uint32_t);
    /* 头部对齐到 8 字节 */
    final_head_offset                   = (uint16_t)MP_ALIGN_UP(raw_head_size, MP_ARCH_ALIGN_SIZE);
    const uint32_t aligned_payload_size = MP_ALIGN_UP(cfg->payload_size, sizeof(uint32_t));
    content_size                        = final_head_offset + aligned_payload_size + raw_tail_size;
#else
    final_head_offset                   = 0;
    content_size                        = cfg->payload_size;
    const uint32_t aligned_payload_size = cfg->payload_size;
#endif

    /* 块步长向上对齐到 CacheLine */
    const uint32_t block_stride    = MP_ALIGN_UP(content_size, MP_CACHE_LINE_SIZE);
    /* 计算剩下的空间，实际能切出多少个完整的块 */

    const uint32_t actual_capacity = available_bytes / block_stride;
    /* 如果实际能装下的块 < 期望的块，报错 */
    if (actual_capacity < cfg->n_blks) {
        return RET_MEM_CODE(RET_CLASS_RESOURCE, RET_R_NO_MEM);
    }

    /* 检查位图大小是否足够 */
    if ((uint32_t)cfg->bm_words * 32u < (uint32_t)cfg->n_blks) {
        return RET_MEM_CODE(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    }
    /* 初始化赋值 */
    p->lock = *cfg->lock;
    if (cfg->cache_ops) {
        p->cache_ops = *cfg->cache_ops;
    } else {
        p->cache_ops.clean      = NULL;
        p->cache_ops.invalidate = NULL;
    }
    p->base             = (uint8_t *)aligned_addr; /* 使用对齐后的基地址 */
    p->n_blks           = cfg->n_blks;             /* 确认数量 */
    p->blk_payload_size = aligned_payload_size;
    p->blk_head_offset  = final_head_offset;
    p->blk_total_size   = (uint16_t)block_stride;
#if MP_CFG_CANARY
    /* 头尾魔数 */
    p->canary_head = 0xA55A1234u;
    p->canary_tail = 0x55AA4321u;
#endif
    /* 各种 Buffer 挂载 */
    p->free_stack = cfg->free_stack;
    p->alloc_bm   = cfg->alloc_bm;
    p->bm_words   = cfg->bm_words;
#if MP_CFG_QUARANTINE
    p->q_ring = cfg->quarantine_buf;
    p->q_cap  = cfg->quarantine_cap;
    p->q_w = p->q_r = p->q_cnt = 0;
#endif
    /* 状态复位 */
    p->top = 0;
    memset(&p->stats, 0, sizeof(p->stats));
    for (uint16_t i = 0; i < cfg->bm_words; i++) p->alloc_bm[i] = 0;
    /* 填充空闲链表 */
    for (uint16_t i = 0; i < cfg->n_blks; i++) {
        p->free_stack[p->top++] = i;
        uint8_t *curr_blk       = p->base + ((uint32_t)i * p->blk_total_size);
        write_canary(p, curr_blk);
    }
    return RET_OK;
}
/**
 * @brief 分配单个块
 * @param p 内存池句柄
 * @return  返回一个分配的块地址
 * @note 返回值可能为NULL 需要检查
 */
void *mp_alloc(mp_pool1_t *p) {
    ASSERT_PARAM(p != NULL);
    if (!p) return NULL;
    /* 上锁 */
    MP_ASSERT(p->lock.unlock && p->lock.handle && p->lock.lock);
    uint32_t flags = 0;
    p->lock.lock(p->lock.handle, &flags);

#if MP_CFG_QUARANTINE
    (void)q_pop_one_to_freelist(p);
#endif
    /* 空闲块不足 */
    if (p->top == 0) {
        p->stats.alloc_fail++;
        /* 解锁 */
        p->lock.unlock(p->lock.handle, &flags);
        return NULL;
    }
    const uint16_t id = p->free_stack[--p->top];
    /* 检测是否被用过了 */
    if (bm_test(p->alloc_bm, id)) {
        p->stats.alloc_fail++;
        /* 解锁 */
        p->lock.unlock(p->lock.handle, &flags);
        return NULL;
    }
    uint8_t *blk = blk_ptr(p, id);
    /* 检测魔数是否还在 内存是否安全 */
    if (check_canary(p, blk) != RET_OK) {
        p->stats.alloc_fail++;
        /* 解锁 */
        p->lock.unlock(p->lock.handle, &flags);
        return NULL;
    }
    /* 位图标记该块已分配 */
    bm_set(p->alloc_bm, id);
    p->stats.inuse++;
    if (p->stats.inuse > p->stats.max_inuse) {
        p->stats.max_inuse = p->stats.inuse;
    }
    p->stats.alloc_ok++;
    /* 退出临界区或 解锁 */
    p->lock.unlock(p->lock.handle, &flags);
    return blk_payload(p, blk);
}
/**
 * @brief 检查块是否在该内存池里
 * @param p 内存句柄
 * @param blk 块基地址
 * @return 块是否在该内存池里
 */
CORE_INLINE bool ptr_in_pool(const mp_pool1_t *p, const uint8_t *blk) {
    const uintptr_t b = (uintptr_t)p->base;
    const uintptr_t e = b + (uintptr_t)p->blk_total_size * (uintptr_t)p->n_blks;
    const uintptr_t x = (uintptr_t)blk;
    return (x >= b) && (x < e) && (((x - b) % p->blk_total_size) == 0u);
}
/**
 * @brief 将指定负载所属块释放
 * @param p 内存池句柄
 * @param payload_ptr 块负载基地址
 * @return 释放接管
 * @note 极致性能必须块数量 等于2的幂次
 */
ret_code_t mp_free(mp_pool1_t *p, void *payload_ptr) {
    ASSERT_PARAM((p != NULL) && (payload_ptr != NULL));
    REQUIRE_RET((p != NULL) && (payload_ptr != NULL),
                RET_MEM_CODE(RET_CLASS_PARAM, RET_R_NULL_PTR));
    /* 上锁 */
    MP_ASSERT(p->lock.unlock && p->lock.handle && p->lock.lock);
    uint32_t flags = 0;
    p->lock.lock(p->lock.handle, &flags);

    uint8_t *blk = payload_to_blk(p, payload_ptr);
    if (!ptr_in_pool(p, blk)) {
        p->stats.free_fail++;
        /* 解锁 */
        p->lock.unlock(p->lock.handle, &flags);
        return RET_MEM_CODE(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    }
    const uint16_t id = (uint16_t)((uintptr_t)blk - (uintptr_t)p->base) / p->blk_total_size;
    /* 防止双重释放 */
    if (!bm_test(p->alloc_bm, id)) {
        p->stats.free_double++;
        /* 解锁 */
        p->lock.unlock(p->lock.handle, &flags);
        return RET_MEM_CODE(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    }
    const ret_code_t rc = check_canary(p, blk);
    /* 防止内存践踏 */
    if (rc != RET_OK) {
        p->stats.free_fail++;
        /* 解锁 */
        p->lock.unlock(p->lock.handle, &flags);
        MP_ASSERT(rc == RET_OK);
    }
    /* 标记释放 */
    bm_clear(p->alloc_bm, id);
    if (p->stats.inuse > 0) {
        p->stats.inuse--;
    }
    p->stats.free_ok++;
    /* 重写 canary */
    write_canary(p, blk);
#if MP_CFG_QUARANTINE
    q_push(p, id);
#else
    p->free_stack[p->top++] = id;
#endif
    /* 解锁 */
    p->lock.unlock(p->lock.handle, &flags);
    return RET_OK;
}
/**
 * @brief
 * @param p 内存池句柄
 * @return 内存池是否安全
 */
ret_code_t mp_check_pool(mp_pool1_t *p) {
    ASSERT_PARAM(p != NULL);
    REQUIRE_RET(p != NULL, RET_MEM_CODE(RET_CLASS_PARAM, RET_R_NULL_PTR));
#if MP_CFG_CANARY
    for (uint16_t i = 0; i < p->n_blks; i++) {
        if (check_canary(p, blk_ptr(p, i)) != RET_OK) {
            return RET_MEM_CODE(RET_CLASS_DATA, RET_R_CHECKSUM);
        }
    }
#endif
    return RET_OK;
}
#endif
