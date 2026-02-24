#include "auto_init.h"

#include <stddef.h>

#include "assert_cus.h"

extern const auto_init_item_t __auto_init_early_start[];
extern const auto_init_item_t __auto_init_early_end[];
extern const auto_init_item_t __auto_init_init_start[];
extern const auto_init_item_t __auto_init_init_end[];
extern const auto_init_item_t __auto_init_late_start[];
extern const auto_init_item_t __auto_init_late_end[];

/* 限制最大单桶得个数 */
#ifndef AUTO_INIT_MAX_PER_BUCKET
#define AUTO_INIT_MAX_PER_BUCKET 64u
#endif
/* 快捷导入块起止地址 */
#define DECL_RANGE(level, b)                                            \
    extern const auto_init_item_t __auto_init_##level##_b##b##_start[]; \
    extern const auto_init_item_t __auto_init_##level##_b##b##_end[]
/* EARLY */
DECL_RANGE(early, 0);
DECL_RANGE(early, 1);
DECL_RANGE(early, 2);
DECL_RANGE(early, 3);
DECL_RANGE(early, 4);
DECL_RANGE(early, 5);
DECL_RANGE(early, 6);
DECL_RANGE(early, 7);
/* INIT */
DECL_RANGE(init, 0);
DECL_RANGE(init, 1);
DECL_RANGE(init, 2);
DECL_RANGE(init, 3);
DECL_RANGE(init, 4);
DECL_RANGE(init, 5);
DECL_RANGE(init, 6);
DECL_RANGE(init, 7);
/* LATE */
DECL_RANGE(late, 0);
DECL_RANGE(late, 1);
DECL_RANGE(late, 2);
DECL_RANGE(late, 3);
DECL_RANGE(late, 4);
DECL_RANGE(late, 5);
DECL_RANGE(late, 6);
DECL_RANGE(late, 7);
static void run_bucket_sorted(const auto_init_item_t *begin, const auto_init_item_t *end) {
    ASSERT_PARAM((begin != NULL) && (end != NULL) && (end >= begin));
    if ((begin == NULL) || (end == NULL) || (end < begin)) return;
    const uintptr_t n = (uintptr_t)(end - begin);
    if (n == 0u) return;

    /* 强约束：超限直接降级为线性执行 */
    if (n > (uintptr_t)AUTO_INIT_MAX_PER_BUCKET) {
        for (const auto_init_item_t *p = begin; p < end; ++p) {
            if (p->fn) p->fn();
        }
        return;
    }

    uint64_t done = 0u;
    for (uintptr_t executed = 0; executed < n; ++executed) {
        uint16_t best_prio = 0xFFFFu;
        uintptr_t best_idx = (uintptr_t)(-1);

        for (uintptr_t i = 0; i < n; ++i) {
            if (done & (1ull << i)) continue;
            const auto_init_item_t *it = &begin[i];
            if (!it->fn) {
                done |= (1ull << i);
                continue;
            }
            if (it->prio < best_prio) {
                best_prio = it->prio;
                best_idx  = i;
            }
        }

        if (best_idx == (uintptr_t)(-1)) {
            break;
        }
        done |= (1ull << best_idx);
        begin[best_idx].fn();
    }
}

static void run_level_early(void) {
    run_bucket_sorted(__auto_init_early_b0_start, __auto_init_early_b0_end);
    run_bucket_sorted(__auto_init_early_b1_start, __auto_init_early_b1_end);
    run_bucket_sorted(__auto_init_early_b2_start, __auto_init_early_b2_end);
    run_bucket_sorted(__auto_init_early_b3_start, __auto_init_early_b3_end);
    run_bucket_sorted(__auto_init_early_b4_start, __auto_init_early_b4_end);
    run_bucket_sorted(__auto_init_early_b5_start, __auto_init_early_b5_end);
    run_bucket_sorted(__auto_init_early_b6_start, __auto_init_early_b6_end);
    run_bucket_sorted(__auto_init_early_b7_start, __auto_init_early_b7_end);
}

static void run_level_init(void) {
    run_bucket_sorted(__auto_init_init_b0_start, __auto_init_init_b0_end);
    run_bucket_sorted(__auto_init_init_b1_start, __auto_init_init_b1_end);
    run_bucket_sorted(__auto_init_init_b2_start, __auto_init_init_b2_end);
    run_bucket_sorted(__auto_init_init_b3_start, __auto_init_init_b3_end);
    run_bucket_sorted(__auto_init_init_b4_start, __auto_init_init_b4_end);
    run_bucket_sorted(__auto_init_init_b5_start, __auto_init_init_b5_end);
    run_bucket_sorted(__auto_init_init_b6_start, __auto_init_init_b6_end);
    run_bucket_sorted(__auto_init_init_b7_start, __auto_init_init_b7_end);
}

static void run_level_late(void) {
    run_bucket_sorted(__auto_init_late_b0_start, __auto_init_late_b0_end);
    run_bucket_sorted(__auto_init_late_b1_start, __auto_init_late_b1_end);
    run_bucket_sorted(__auto_init_late_b2_start, __auto_init_late_b2_end);
    run_bucket_sorted(__auto_init_late_b3_start, __auto_init_late_b3_end);
    run_bucket_sorted(__auto_init_late_b4_start, __auto_init_late_b4_end);
    run_bucket_sorted(__auto_init_late_b5_start, __auto_init_late_b5_end);
    run_bucket_sorted(__auto_init_late_b6_start, __auto_init_late_b6_end);
    run_bucket_sorted(__auto_init_late_b7_start, __auto_init_late_b7_end);
}

void auto_init_run_level(auto_init_level_t level) {
    ASSERT_PARAM(level <= AUTO_INIT_LATE);
    if (level > AUTO_INIT_LATE) return;
    switch (level) {
        case AUTO_INIT_EARLY:
            run_level_early();
            break;
        case AUTO_INIT_INIT:
            run_level_init();
            break;
        case AUTO_INIT_LATE:
        default:
            run_level_late();
            break;
    }
}

void auto_init_run_all(void) {
    run_level_early();
    run_level_init();
    run_level_late();
}
