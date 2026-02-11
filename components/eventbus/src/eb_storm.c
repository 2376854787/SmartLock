#include "eb_storm.h"

#if (EB_CFG_ENABLE_STORM == 1)

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

static inline bool try_lock_u32(volatile uint32_t* lock_word) {
    /* Never spin here (ISR-safe). If contended, just fail and degrade. */
    const uint32_t prev = __atomic_exchange_n((uint32_t*)lock_word, 1u, __ATOMIC_ACQUIRE);
    return (prev == 0u);
}

static inline void unlock_u32(volatile uint32_t* lock_word) {
    __atomic_store_n((uint32_t*)lock_word, 0u, __ATOMIC_RELEASE);
}

static inline uint32_t storm_hash(uint32_t event_id, uint16_t source_id) {
    uint32_t x = event_id ^ ((uint32_t)source_id << 16);
    /* xorshift */
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

static eb_storm_slot_t* find_slot(uint32_t event_id, uint16_t source_id) {
    const uint32_t h = storm_hash(event_id, source_id);
    if (EB_STORM_SLOTS == 0u) return NULL;

    uint32_t i = h % (uint32_t)EB_STORM_SLOTS;
    for (uint32_t probe = 0; probe < (uint32_t)EB_STORM_SLOTS; probe++) {
        eb_storm_slot_t* s = &g_slots[i];

        const uint32_t st  = __atomic_load_n(&s->state, __ATOMIC_ACQUIRE);
        if (st == 0u) {
            uint32_t expect = 0u;
            if (__atomic_compare_exchange_n(&s->state, &expect, 1u, false, __ATOMIC_ACQ_REL,
                                            __ATOMIC_ACQUIRE)) {
                /* We own initialization of this slot */
                s->hkey           = h;
                s->last_ms        = 0u;
                s->last_refill_ms = 0u;
                s->tokens         = 0u;
                s->lock           = 0u;
                __atomic_store_n(&s->state, 2u, __ATOMIC_RELEASE);
                return s;
            }
        } else if (st == 2u) {
            /* Only trust hkey when slot is ready */
            if (s->hkey == h) return s;
        }

        i = (i + 1u) % (uint32_t)EB_STORM_SLOTS;
    }
    /* 表满：退化为“不限频”避免阻塞 */
    return NULL;
}

void eb_storm_init(void) {
    memset(g_slots, 0, sizeof(g_slots));
}

bool eb_storm_allow(const eb_eventdef_t* def, uint16_t source_id, uint32_t now_ms) {
    if (!def) return true;

    if (def->storm_policy == EB_STORM_NONE) return true;

    eb_storm_slot_t* st = find_slot(def->event_id, source_id);
    if (!st) return true; /* 退化策略：不限频 */

    /* Slot update must be concurrency-safe under multi-producer publish.
     * This is best-effort: on contention, we allow (degrade) to avoid blocking ISR/tasks. */
    if (!try_lock_u32(&st->lock)) {
        return true;
    }

    if (def->storm_policy == EB_STORM_MIN_INTERVAL) {
        const uint16_t min_ms = def->storm_min_interval_ms;
        if (min_ms == 0u) {
            unlock_u32(&st->lock);
            return true;
        }
        if ((uint32_t)(now_ms - st->last_ms) < (uint32_t)min_ms) {
            unlock_u32(&st->lock);
            return false;
        }
        st->last_ms = now_ms;
        unlock_u32(&st->lock);
        return true;
    }

    /* TOKEN_BUCKET */
    const uint16_t cap           = def->storm_tb_capacity;
    const uint16_t refill_ms     = def->storm_refill_ms;
    const uint16_t refill_tokens = def->storm_refill_tokens;

    if (cap == 0u || refill_ms == 0u || refill_tokens == 0u) {
        unlock_u32(&st->lock);
        return true;
    }

    if (!st->last_refill_ms) {
        st->last_refill_ms = now_ms;
        st->tokens         = (cap > 0u) ? (uint16_t)(cap) : 0u;
    } else {
        const uint32_t elapsed = (uint32_t)(now_ms - st->last_refill_ms);
        if (elapsed >= refill_ms) {
            const uint32_t k          = elapsed / refill_ms;
            const uint32_t add        = k * (uint32_t)refill_tokens;
            const uint32_t new_tokens = (uint32_t)st->tokens + add;
            st->tokens                = (new_tokens > cap) ? cap : (uint16_t)new_tokens;
            st->last_refill_ms += k * (uint32_t)refill_ms;
        }
    }

    if (st->tokens == 0u) {
        unlock_u32(&st->lock);
        return false;
    }
    st->tokens--;
    unlock_u32(&st->lock);
    return true;
}

#endif /* EB_CFG_ENABLE_STORM */
