#ifndef SMARTLOCK_PERF_H
#define SMARTLOCK_PERF_H
#include <stdint.h>

#include "compiler_cus.h"
#include "hal_time.h"

typedef struct {
    uint32_t count;
    uint32_t last_us;
    uint16_t min_us;
    uint16_t max_us;
    uint64_t sum_us;
} perf_stat_t;
typedef struct {
    uint32_t start_cyc;
} perf_scope_t;
CORE_USED static inline void perf_scope_begin(perf_scope_t *s) {
    s->start_cyc = hal_get_cycle32();
}
CORE_USED static inline uint32_t perf_scope_end_us(const perf_scope_t *s) {
    const uint32_t end_cyc = hal_get_cycle32();
    return hal_cycles_to_us((uint32_t)(end_cyc - s->start_cyc));
}
void perf_stat_reset(perf_stat_t *st);
void perf_stat_update(perf_stat_t *st, uint32_t dt_us);
#endif  // SMARTLOCK_PERF_H
