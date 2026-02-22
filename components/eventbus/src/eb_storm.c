#include "eb_storm.h"

#if (defined(EB_CFG_ENABLE_STORM) && (EB_CFG_ENABLE_STORM == 1))

#include <string.h>

typedef struct {
    uint32_t hkey;           /* 哈希值 判断事件id 和 源id是否一致 */
    uint32_t last_ms;        /* 上次放行时间 */
    uint32_t last_refill_ms; /* 上次令牌补充时间 */
    uint16_t tokens;         /* 当前剩余令牌数量 */
    uint16_t _pad16;
    /* 状态机： 0=空闲, 1=正在初始化, 2=就绪 */
    uint32_t state;
    /* 锁槽  */
    uint32_t lock;
} eb_storm_slot_t;

static eb_storm_slot_t g_slots[EB_STORM_SLOTS];
/**
 * @brief 获取锁
 * @param lock_word lock字段
 * @return
 */
static inline bool try_lock_u32(volatile uint32_t* lock_word) {
    /* Never spin here (ISR-safe). If contended, just fail and degrade. */
    const uint32_t prev = __atomic_exchange_n((uint32_t*)lock_word, 1u, __ATOMIC_ACQUIRE);
    return (prev == 0u);
}
/**
 * @brief 释放锁
 * @param lock_word 锁字段
 */
static inline void unlock_u32(volatile uint32_t* lock_word) {
    __atomic_store_n((uint32_t*)lock_word, 0u, __ATOMIC_RELEASE);
}
/**
 * @brief 哈希事件id和其源id
 * @param event_id 事件id
 * @param source_id 源id
 * @return
 */
static inline uint32_t storm_hash(uint32_t event_id, uint16_t source_id) {
    uint32_t x = event_id ^ ((uint32_t)source_id << 16);
    /* xorshift */
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}
/**
 * @brief 查询当前投递源和事件组合的频率等状态
 * @param event_id 事件id
 * @param source_id 源id
 * @return 返回这个组合的限流状态
 */
static eb_storm_slot_t* find_slot(uint32_t event_id, uint16_t source_id) {
    const uint32_t h = storm_hash(event_id, source_id);
    if (EB_STORM_SLOTS == 0u) return NULL;
    /* 哈希过后求索引 */
    uint32_t i = h % (uint32_t)EB_STORM_SLOTS;
    /* 找到当前哈希后的索引应该存放在那个位置 并初始化*/
    for (uint32_t probe = 0; probe < (uint32_t)EB_STORM_SLOTS; probe++) {
        eb_storm_slot_t* s = &g_slots[i];
        /* 找出当前的状态 */
        const uint32_t st  = __atomic_load_n(&s->state, __ATOMIC_ACQUIRE);
        /* 槽位没有人使用 */
        if (st == 0u) {
            uint32_t expect = 0u;
            /* 发现依然是0 抢占成功修改状态为1 初始化中 */
            if (__atomic_compare_exchange_n(&s->state, &expect, 1u, false, __ATOMIC_ACQ_REL,
                                            __ATOMIC_ACQUIRE)) {
                /* 初始化该槽位 */
                s->hkey           = h;
                s->last_ms        = 0u;
                s->last_refill_ms = 0u;
                s->tokens         = 0u;
                s->lock           = 0u;
                __atomic_store_n(&s->state, 2u, __ATOMIC_RELEASE);
                return s;
            }
        } else if (st == 2u) {
            /* 槽位已经准备好了 */
            if (s->hkey == h) return s;
        }

        i = (i + 1u) % (uint32_t)EB_STORM_SLOTS;
    }
    /* 表满：退化为“不限频”避免阻塞 */
    return NULL;
}
/**
 * @brief 初始化防护桶
 */
void eb_storm_init(void) {
    memset(g_slots, 0, sizeof(g_slots));
}
/**
 *
 * @param def 事件策略
 * @param source_id 源id
 * @param now_ms 现在的时间
 * @return
 */
bool eb_storm_allow(const eb_eventdef_t* def, uint16_t source_id, uint32_t now_ms) {
    /* 无策略直接通过 */
    if (!def) return true;
    /* 不开启限流直接通过 */
    if (def->storm_policy == EB_STORM_NONE) return true;
    /* 找到当前的状态 */
    eb_storm_slot_t* st = find_slot(def->event_id, source_id);
    if (!st) return true; /* 退化策略：不限频 */
    /* 抢锁失败直接通过 */
    if (!try_lock_u32(&st->lock)) {
        return true;
    }
    /* 最小间隔限流策略 */
    if (def->storm_policy == EB_STORM_MIN_INTERVAL) {
        const uint16_t min_ms = def->storm_min_interval_ms;
        if (min_ms == 0u) {
            unlock_u32(&st->lock);
            return true;
        }
        /* 时间没到*/
        if ((uint32_t)(now_ms - st->last_ms) < (uint32_t)min_ms) {
            unlock_u32(&st->lock);
            return false;
        }
        /* 时间到了放行 */
        st->last_ms = now_ms;
        unlock_u32(&st->lock);
        return true;
    }

    /* 令牌限流 */
    const uint16_t cap           = def->storm_tb_capacity;
    const uint16_t refill_ms     = def->storm_refill_ms;
    const uint16_t refill_tokens = def->storm_refill_tokens;

    /* 不限流策略 */
    if (cap == 0u || refill_ms == 0u || refill_tokens == 0u) {
        unlock_u32(&st->lock);
        return true;
    }
    /* 初始状态 根据策略填充有效数据*/
    if (!st->last_refill_ms) {
        st->last_refill_ms = now_ms;
        st->tokens         = (cap > 0u) ? (uint16_t)(cap) : 0u;
    } else { /* 非初始状态 */
        /* 计算已经过去的时间 */
        const uint32_t elapsed = (uint32_t)(now_ms - st->last_refill_ms);
        /* 令牌补充间隔到了 */
        if (elapsed >= refill_ms) {
            const uint32_t k          = elapsed / refill_ms;
            const uint32_t add        = k * (uint32_t)refill_tokens;
            const uint32_t new_tokens = (uint32_t)st->tokens + add;
            st->tokens                = (new_tokens > cap) ? cap : (uint16_t)new_tokens;
            st->last_refill_ms += k * (uint32_t)refill_ms;
        }
    }
    /* 减少令牌通过事件投递 */
    if (st->tokens == 0u) {
        unlock_u32(&st->lock);
        return false;
    }
    st->tokens--;
    unlock_u32(&st->lock);
    return true;
}

#endif /* EB_CFG_ENABLE_STORM */

