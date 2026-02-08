#ifndef SMARTLOCK_EB_API_H
#define SMARTLOCK_EB_API_H
#include "eb_types.h"

/* 初始化：清空队列与计数器 */
void eb_init(void);

/* 发布：多生产者；先用临界区实现正确性 */
eb_ret_t eb_publish(const eb_event_t* ev);

/* BusTask 每次调用处理一批事件（按 H/M/L + L配额） */
void eb_pump_once(void);

/* 统计：后续 trace/ops 要用 */
typedef struct {
    uint32_t enq_h, enq_m, enq_l;
    uint32_t deq_h, deq_m, deq_l;
    uint32_t drop_h, drop_m, drop_l; /* 队列满丢弃计数 */
} eb_stats_t;

const eb_stats_t* eb_get_stats(void);
void eb_dispatch(const eb_event_t* ev);

#endif  // SMARTLOCK_EB_API_H
