/* TODO: Budget Police 直方图精度依赖 eb_port_timestamp_us() 的真实 µs 分辨率。
 *       当前 weak 实现退化为 ms*1000，导致所有 <1ms 事件落入桶 0，p50/p95/p99 无意义。
 *       在平台层实现 DWT/TIM 级 µs 时间戳后，此模块数据才具备生产诊断价值。
 */
#include "eb_budget.h"

#if (EB_CFG_ENABLE_BUDGET == 1)

#include <string.h>

#ifndef EB_BUDGET_BUCKET_US
#define EB_BUDGET_BUCKET_US 50u
#endif
#ifndef EB_BUDGET_BUCKETS
#define EB_BUDGET_BUCKETS 64u
#endif

_Static_assert(EB_BUDGET_BUCKET_US > 0u, "EB_BUDGET_BUCKET_US must > 0");
_Static_assert(EB_BUDGET_BUCKETS > 0u, "EB_BUDGET_BUCKETS must > 0");

/* 聚合维度：按优先级（H/M/L）统计 dispatch 耗时；另有 round 耗时。 */
enum { EB_BUDGET_PRIO_DIM = 3u };

static volatile uint32_t g_hist_event[EB_BUDGET_PRIO_DIM][EB_BUDGET_BUCKETS];
static volatile uint32_t g_hist_round[EB_BUDGET_BUCKETS];

static volatile uint32_t g_count_event[EB_BUDGET_PRIO_DIM];
static volatile uint32_t g_count_round;

static volatile uint32_t g_max_event[EB_BUDGET_PRIO_DIM];
static volatile uint32_t g_max_round;

static inline uint32_t clamp_bucket(uint32_t dur_us) {
    const uint32_t w = (uint32_t)EB_BUDGET_BUCKET_US;
    uint32_t idx     = dur_us / w;
    if (idx >= (uint32_t)EB_BUDGET_BUCKETS) idx = (uint32_t)EB_BUDGET_BUCKETS - 1u;
    return idx;
}

static inline uint32_t prio_idx(eb_prio_t p) {
    switch (p) {
        case EB_PRIO_H:
            return 0u;
        case EB_PRIO_M:
            return 1u;
        default:
            return 2u;
    }
}

static inline void atomic_max_u32(volatile uint32_t* dst, uint32_t v) {
    /* relaxed CAS loop：无锁更新最大值；写入不会阻塞。 */
    uint32_t cur = __atomic_load_n((uint32_t*)dst, __ATOMIC_RELAXED);
    while (v > cur) {
        if (__atomic_compare_exchange_n((uint32_t*)dst, &cur, v, false, __ATOMIC_RELAXED,
                                        __ATOMIC_RELAXED)) {
            break;
        }
        /* cur 已被更新为最新值，继续比较 */
    }
}

void eb_budget_init(void) {
    eb_budget_reset();
}

void eb_budget_reset(void) {
    memset((void*)g_hist_event, 0, sizeof(g_hist_event));
    memset((void*)g_hist_round, 0, sizeof(g_hist_round));

    for (uint32_t i = 0; i < EB_BUDGET_PRIO_DIM; i++) {
        __atomic_store_n((uint32_t*)&g_count_event[i], 0u, __ATOMIC_RELAXED);
        __atomic_store_n((uint32_t*)&g_max_event[i], 0u, __ATOMIC_RELAXED);
    }
    __atomic_store_n((uint32_t*)&g_count_round, 0u, __ATOMIC_RELAXED);
    __atomic_store_n((uint32_t*)&g_max_round, 0u, __ATOMIC_RELAXED);
}

void eb_budget_record_event(eb_prio_t prio, uint32_t dur_us) {
    const uint32_t pi = prio_idx(prio);
    const uint32_t b  = clamp_bucket(dur_us);

    (void)__atomic_fetch_add((uint32_t*)&g_hist_event[pi][b], 1u, __ATOMIC_RELAXED);
    (void)__atomic_fetch_add((uint32_t*)&g_count_event[pi], 1u, __ATOMIC_RELAXED);
    atomic_max_u32(&g_max_event[pi], dur_us);
}

void eb_budget_record_round(uint32_t dur_us) {
    const uint32_t b = clamp_bucket(dur_us);

    (void)__atomic_fetch_add((uint32_t*)&g_hist_round[b], 1u, __ATOMIC_RELAXED);
    (void)__atomic_fetch_add((uint32_t*)&g_count_round, 1u, __ATOMIC_RELAXED);
    atomic_max_u32(&g_max_round, dur_us);
}

static inline uint32_t bucket_upper_bound_us(uint32_t bucket) {
    /* bucket i -> (i+1)*W，最后一个桶是溢出桶，返回 B*W 作为下界上界的近似 */
    const uint32_t w = (uint32_t)EB_BUDGET_BUCKET_US;
    const uint32_t b = (uint32_t)EB_BUDGET_BUCKETS;
    if (bucket >= b - 1u) {
        return b * w;
    }
    return (bucket + 1u) * w;
}

static eb_budget_stats_t query_hist(const volatile uint32_t* hist, uint32_t count,
                                    uint32_t max_us) {
    eb_budget_stats_t out = {0};
    out.count             = count;
    out.max_us            = max_us;
    if (count == 0u) return out;

    /* 目标：找到累计计数达到阈值时的桶 */
    const uint32_t t50 = (count + 1u) / 2u;          /* ceil(0.50*count) */
    const uint32_t t95 = (count * 95u + 99u) / 100u; /* ceil(0.95*count) */
    const uint32_t t99 = (count * 99u + 99u) / 100u; /* ceil(0.99*count) */

    uint32_t c         = 0u;
    uint32_t p50_b = 0u, p95_b = 0u, p99_b = 0u;
    bool got50 = false, got95 = false, got99 = false;

    for (uint32_t i = 0u; i < (uint32_t)EB_BUDGET_BUCKETS; i++) {
        const uint32_t v = __atomic_load_n((const uint32_t*)&hist[i], __ATOMIC_RELAXED);
        if (v == 0u) continue;
        c += v;

        if (!got50 && c >= t50) {
            p50_b = i;
            got50 = true;
        }
        if (!got95 && c >= t95) {
            p95_b = i;
            got95 = true;
        }
        if (!got99 && c >= t99) {
            p99_b = i;
            got99 = true;
            break;
        }
    }

    out.p50_us = bucket_upper_bound_us(p50_b);
    out.p95_us = bucket_upper_bound_us(p95_b);
    out.p99_us = bucket_upper_bound_us(p99_b);
    return out;
}

eb_budget_stats_t eb_budget_query_event(eb_prio_t prio) {
    const uint32_t pi  = prio_idx(prio);
    const uint32_t cnt = __atomic_load_n((uint32_t*)&g_count_event[pi], __ATOMIC_RELAXED);
    const uint32_t mx  = __atomic_load_n((uint32_t*)&g_max_event[pi], __ATOMIC_RELAXED);
    return query_hist(g_hist_event[pi], cnt, mx);
}

eb_budget_stats_t eb_budget_query_round(void) {
    const uint32_t cnt = __atomic_load_n((uint32_t*)&g_count_round, __ATOMIC_RELAXED);
    const uint32_t mx  = __atomic_load_n((uint32_t*)&g_max_round, __ATOMIC_RELAXED);
    return query_hist(g_hist_round, cnt, mx);
}

const uint32_t* eb_budget_hist_event(eb_prio_t prio) {
    const uint32_t pi = prio_idx(prio);
    return (const uint32_t*)&g_hist_event[pi][0];
}

const uint32_t* eb_budget_hist_round(void) {
    return (const uint32_t*)&g_hist_round[0];
}

uint32_t eb_budget_bucket_us(void) {
    return (uint32_t)EB_BUDGET_BUCKET_US;
}

uint32_t eb_budget_buckets(void) {
    return (uint32_t)EB_BUDGET_BUCKETS;
}

#endif /* EB_CFG_ENABLE_BUDGET */
