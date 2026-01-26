#include "APP_config.h"
#if defined(ENABLE_MEMORY_POOL)
#include <stdbool.h>
#include <string.h>

#include "compiler_cus.h"
#include "memory_pool.h"

#define MP_ALIGN      8u
#define MP_MAGIC_BASE 0x4D504F4Fu  // 'MPOO' 随便选一个

static inline uint32_t align_up_u32(uint32_t x, uint32_t a) {
    return (x + a - 1u) & ~(a - 1u);
}

/**
 * @brief 下一块的地址与全局密钥以及自身的地址进行异或加密
 * @param mgr
 * @param hdr
 * @param next
 * @return
 */
static inline uintptr_t enc_next(const mp_mgr_t *mgr, const mp_block_hdr_t *hdr,
                                 const mp_block_hdr_t *next) {
    return ((uintptr_t)next) ^ (uintptr_t)mgr->boot_key ^ (uintptr_t)hdr;
}
/**
 * @brief 通过密钥和当前地址还原出真正的下一块空闲地址
 * @param mgr
 * @param hdr
 * @return
 */
static inline mp_block_hdr_t *dec_next(const mp_mgr_t *mgr, const mp_block_hdr_t *hdr) {
    return (mp_block_hdr_t *)(hdr->next_enc ^ (uintptr_t)mgr->boot_key ^ (uintptr_t)hdr);
}
/**
 * @brief 判断当前内存池是否还有空闲
 * @param p
 * @return
 */
static inline uint8_t *pool_end(const mp_pool_t *p) {
    return p->base + (size_t)p->stride * p->blk_count;
}
/**
 * @brief 判断当前
 * @param p
 * @param ptr
 * @return
 */
static bool ptr_in_pool(const mp_pool_t *p, const void *ptr) {
    const uint8_t *u = (const uint8_t *)ptr;
    return (u >= p->base) && (u < pool_end(p));
}

static bool ptr_aligned_to_block(const mp_pool_t *p, const void *ptr) {
    const uintptr_t u        = (uintptr_t)ptr;
    const uintptr_t payload0 = (uintptr_t)p->base + sizeof(mp_block_hdr_t);
    if (u < payload0) return false;
    return ((u - payload0) % p->stride) == 0;
}

void mp_pool_init(mp_mgr_t *mgr, mp_pool_t *p, uint8_t *mem, uint32_t payload_size,
                  uint32_t blk_count, uint16_t class_id) {
    p->base          = mem;
    p->blk_size      = payload_size;
    p->stride        = align_up_u32((uint32_t)sizeof(mp_block_hdr_t) + payload_size, MP_ALIGN);
    p->blk_count     = blk_count;
    p->magic         = MP_MAGIC_BASE ^ (uint32_t)class_id;

    p->free_head     = NULL;
    p->free_blocks   = blk_count;
    p->min_free_ever = blk_count;
    p->fail_count    = 0;

    // 把所有块串成 freelist（LIFO）
    for (uint32_t i = 0; i < blk_count; i++) {
        uint8_t *blk        = p->base + (size_t)i * p->stride;
        mp_block_hdr_t *hdr = (mp_block_hdr_t *)blk;
        hdr->magic          = p->magic;
        hdr->class_id       = class_id;
        hdr->state          = MP_STATE_FREE;

        hdr->next_enc       = enc_next(mgr, hdr, p->free_head);
        p->free_head        = hdr;
    }
}

void *mp_alloc(mp_mgr_t *mgr, mp_pool_t *p) {
    if (!p->free_head) {
        p->fail_count++;
        return NULL;
    }

    mp_block_hdr_t *hdr  = p->free_head;
    mp_block_hdr_t *next = dec_next(mgr, hdr);

    // 合法性校验（SOP：pop/push 校验合法性，异常冻结）
    if (next && !ptr_in_pool(p, next)) {
        // 这里你可以改成断言/冻结现场
        p->fail_count++;
        return NULL;
    }

    p->free_head = next;

    // state 检查
    if (hdr->magic != p->magic || hdr->state != MP_STATE_FREE) {
        p->fail_count++;
        return NULL;
    }

    hdr->state       = MP_STATE_ALLOC;

    // Poison: alloc 填 0xCC（可选）
    uint8_t *payload = (uint8_t *)hdr + sizeof(mp_block_hdr_t);
    memset(payload, 0xCC, p->blk_size);

    // 统计
    p->free_blocks--;
    if (p->free_blocks < p->min_free_ever) p->min_free_ever = p->free_blocks;

    return payload;
}

bool mp_free(mp_mgr_t *mgr, mp_pool_t *p, void *ptr) {
    if (!ptr) return false;

    // SOP：Range Check + 对齐检查
    if (!ptr_in_pool(p, ptr)) return false;
    if (!ptr_aligned_to_block(p, ptr)) return false;

    mp_block_hdr_t *hdr = (mp_block_hdr_t *)((uint8_t *)ptr - sizeof(mp_block_hdr_t));

    // Magic 校验
    if (hdr->magic != p->magic) return false;

    // SOP：State 检查防 double free
    if (hdr->state != MP_STATE_ALLOC) return false;

    // Poison: free 填 0xA5（可选）
    memset(ptr, 0xA5, p->blk_size);

    hdr->state    = MP_STATE_FREE;

    // push 回 freelist（加密 next）
    hdr->next_enc = enc_next(mgr, hdr, p->free_head);
    p->free_head  = hdr;

    p->free_blocks++;
    return true;
}

#endif
