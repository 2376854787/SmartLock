#ifndef SMARTLOCK_EB_TRACE_H
#define SMARTLOCK_EB_TRACE_H

#include <stdbool.h>
#include <stdint.h>

#include "eb_config.h"
#include "eb_port.h"
#include "eb_types.h"

#if (defined(EB_CFG_ENABLE_TRACE) && (EB_CFG_ENABLE_TRACE == 1))

/* 链路追踪 */
typedef enum {
    EB_TRACE_PUB           = 0,
    EB_TRACE_ENQ           = 1,
    EB_TRACE_DROP          = 2,
    EB_TRACE_DEQ           = 3,
    EB_TRACE_DISPATCH      = 4,
    EB_TRACE_MB_OK         = 5,
    EB_TRACE_MB_DROP       = 6,
    EB_TRACE_MB_OW         = 7,
    EB_TRACE_TYPE_MISMATCH = 8,
    EB_TRACE_STORM_DROP    = 9,
} eb_trace_phase_t;

/* 丢弃原因 */
typedef enum {
    EB_DROP_REASON_NONE      = 0,
    EB_DROP_REASON_BUSQ_FULL = 1,
    EB_DROP_REASON_MB_FULL   = 2,
    EB_DROP_REASON_STORM     = 3,
    EB_DROP_REASON_TYPE      = 4,
} eb_drop_reason_t;

/* 日志头 */
typedef struct {
    uint32_t magic;        /* 魔数 */
    uint16_t version;      /* 版本 */
    uint16_t reset_reason; /* 复位原因 */
    uint32_t depth;        /* 深度 */
    uint32_t write_idx;    /* 单调递增 */
    uint32_t wrap_cnt;     /* 回卷次数 */
} eb_trace_header_t;
/* 日志条目 */
typedef struct {
    uint32_t ts_us;       /* 时间戳 */
    uint32_t event_id;    /* 时间id */
    uint32_t key;         /* 过滤key */
    uint32_t payload_u32; /* 负载 */
    uint16_t source_id;   /* 发布源id */
    uint16_t type_tag;    /* 类型 */

    uint8_t prio;        /* 优先级 */
    uint8_t phase;       /* 阶段 */
    uint8_t drop_reason; /* 丢弃原因 */
    uint8_t reserved;

    uint32_t seq;    /* 序列号 */
    uint32_t commit; /* 提交标记 */
} eb_trace_entry_t;

/* 提交标记 */
#ifndef EB_TRACE_COMMIT_MAGIC
#define EB_TRACE_COMMIT_MAGIC (0xC0DEC17Au)
#endif

void eb_trace_init(void);
void eb_trace_clear(void);
void eb_trace_record(eb_trace_phase_t phase, const eb_event_t* ev, eb_drop_reason_t reason);

const eb_trace_header_t* eb_trace_header(void);
const eb_trace_entry_t* eb_trace_entries(void);

#endif /* EB_CFG_ENABLE_TRACE */

#endif  // SMARTLOCK_EB_TRACE_H

