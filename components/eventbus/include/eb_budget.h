#ifndef EB_BUDGET_H
#define EB_BUDGET_H

#include <stdint.h>

#include "eb_config.h"
#include "eb_types.h"

#if (EB_CFG_ENABLE_BUDGET == 1)

/* Budget Police：性能证据链（直方图）。
 * - 记录：dispatch 单次耗时（按 prio 聚合）+ 每轮 pump 总耗时。
 * - 记录路径：O(1)、无阻塞、无动态内存。
 * - 读取/计算：允许 O(桶数)（只在诊断/低优先级调用）。
 */

typedef struct {
    uint32_t count;   /* 样本数 */
    uint32_t p50_us;
    uint32_t p95_us;
    uint32_t p99_us;
    uint32_t max_us;
} eb_budget_stats_t;

/* 初始化（零化直方图与统计）。必须在系统进入多线程/中断前调用。 */
void eb_budget_init(void);

/* 清零所有直方图与 max 统计（保留配置常量不变）。 */
void eb_budget_reset(void);

/* 记录一条“事件 dispatch 耗时”（us）。prio 按 H/M/L 聚合。 */
void eb_budget_record_event(eb_prio_t prio, uint32_t dur_us);

/* 记录一条“eb_pump_once() 总耗时”（us）。 */
void eb_budget_record_round(uint32_t dur_us);

/* 计算当前统计（p50/p95/p99/max/count）。O(桶数)。 */
eb_budget_stats_t eb_budget_query_event(eb_prio_t prio);
eb_budget_stats_t eb_budget_query_round(void);

/* 直方图读出（用于离线分析/CLI dump）。 */
const uint32_t* eb_budget_hist_event(eb_prio_t prio);
const uint32_t* eb_budget_hist_round(void);

/* 参数查询（便于工具解释桶边界）。 */
uint32_t eb_budget_bucket_us(void);
uint32_t eb_budget_buckets(void);

#endif /* EB_CFG_ENABLE_BUDGET */

#endif /* EB_BUDGET_H */
