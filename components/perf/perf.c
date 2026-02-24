#include "perf.h"

#include <stddef.h>

#include "assert_cus.h"

/**
 * @brief 将函数运行统计健康信息重置
 * @param st st句柄
 */
void perf_stat_reset(perf_stat_t *st) {
    ASSERT_PARAM(st != NULL);
    REQUIRE_RET_VOID(st != NULL);
    st->count   = 0;
    st->last_us = 0;
    st->min_us  = 0xFFFFu;
    st->max_us  = 0;
    st->sum_us  = 0;
}
/**
 *
 * @param st ST句柄指针
 * @param dt_us 更新上次的us
 */
void perf_stat_update(perf_stat_t *st, uint32_t dt_us) {
    ASSERT_PARAM(st != NULL);
    REQUIRE_RET_VOID(st != NULL);
    st->last_us = dt_us;
    st->count++;
    if (dt_us < st->min_us) st->min_us = dt_us;
    if (dt_us > st->max_us) st->max_us = dt_us;
    st->sum_us += dt_us;
}
