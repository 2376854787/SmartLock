#ifndef SMARTLOCK_EB_EVENTDEF_H
#define SMARTLOCK_EB_EVENTDEF_H
#include <stdint.h>

#include "eb_types.h" /* eb_prio_t */

/* 数据传输类型 */
typedef enum { EB_PLANE_CONTROL = 0, EB_PLANE_DATA = 1 } eb_plane_t;

/* 事件语义 */
typedef enum {
    EB_SEM_EDGE     = 0, /* 每次都是有效信号：不可丢弃/合并 */
    EB_SEM_SNAPSHOT = 1  /* 状态快照：可覆盖/合并，只要求最新 */
} eb_semantic_t;

/* 丢弃策略（当队列/邮箱满） */
typedef enum {
    EB_DROP_NEW        = 0, /* 丢弃新数据（Edge 强制） */
    EB_OVERWRITE       = 1, /* 覆盖旧数据（仅 Snapshot 允许） */
    EB_COALESCE_LATEST = 2  /* 合并最新（仅 Snapshot 允许；当前实现等价 overwrite latest） */
} eb_drop_policy_t;

/* Storm 限频策略 */
typedef enum {
    EB_STORM_NONE         = 0,
    EB_STORM_MIN_INTERVAL = 1, /* 最小间隔：ms */
    EB_STORM_TOKEN_BUCKET = 2, /* 令牌桶：容量/补充周期 */
} eb_storm_policy_t;

/* 事件定义策略 */
typedef struct {
    uint32_t event_id;

    eb_plane_t plane;             /* Control/Data */
    eb_prio_t prio;               /* 强制优先级：eb_publish 会用此值覆盖调用方传入的 prio */
    eb_semantic_t semantic;       /* Edge/Snapshot */
    eb_drop_policy_t drop_policy; /* Edge 强制 DROP_NEW；Snapshot 才允许覆盖/合并 */

    /* Typed API：type_tag==0 表示不做类型约束 */
    uint16_t type_tag;

    /* Storm Policy：按 (event_id, source_id) 限频。source_id 在 eb_event_t 内携带 */
    eb_storm_policy_t storm_policy;
    uint16_t storm_min_interval_ms; /* 最小间隔  */

    uint16_t storm_tb_capacity;   /* 令牌剩余容量 */
    uint16_t storm_refill_ms;     /* 令牌补充事件间隔 */
    uint16_t storm_refill_tokens; /* 每次补充的数量 */
} eb_eventdef_t;

/* 初始化：构建 eventmap（需在 eb_init 中调用） */
void eb_eventdef_init(void);

uint32_t eb_eventdef_count(void);
int32_t eb_event_index(uint32_t event_id);
const eb_eventdef_t* eb_eventdef_get(uint32_t event_id);

#endif  // SMARTLOCK_EB_EVENTDEF_H
