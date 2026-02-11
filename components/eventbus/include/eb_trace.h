#ifndef SMARTLOCK_EB_TRACE_H
#define SMARTLOCK_EB_TRACE_H

#include <stdbool.h>
#include <stdint.h>

#include "eb_config.h"
#include "eb_port.h"
#include "eb_types.h"

#if (EB_CFG_ENABLE_TRACE == 1)

/* Trace phase：用于复盘链路（publish/enq/deq/dispatch/mailbox/drop） */
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

/* Drop reason：用于统计/故障定位（注意：不要使用 printf/IO） */
typedef enum {
    EB_DROP_REASON_NONE      = 0,
    EB_DROP_REASON_BUSQ_FULL = 1,
    EB_DROP_REASON_MB_FULL   = 2,
    EB_DROP_REASON_STORM     = 3,
    EB_DROP_REASON_TYPE      = 4,
} eb_drop_reason_t;

/* header：放在 noinit，用于跨 reset 保留 */
typedef struct {
    uint32_t magic;        /* 'EBTR' */
    uint16_t version;      /* 0x0001 */
    uint16_t reset_reason; /* eb_reset_reason_t */
    uint32_t depth;        /* EB_TRACE_DEPTH */
    uint32_t write_idx;    /* 单调递增 */
    uint32_t wrap_cnt;     /* 回卷次数 */
} eb_trace_header_t;

typedef struct {
    uint32_t ts_us;
    uint32_t event_id;
    uint32_t key;
    uint32_t payload_u32;
    uint16_t source_id;
    uint16_t type_tag;

    uint8_t prio;
    uint8_t phase;
    uint8_t drop_reason;
    uint8_t reserved;

    /* --- Top-tier hardening fields (multi-producer safe) ---
     * seq:    monotonic sequence number (== header.write_idx at reservation time)
     * commit: set to EB_TRACE_COMMIT_MAGIC after all fields are written
     *
     * Reader rule (post-mortem tooling):
     *   - Ignore entries with commit != EB_TRACE_COMMIT_MAGIC
     *   - Optionally verify seq continuity for corruption detection
     */
    uint32_t seq;
    uint32_t commit;
} eb_trace_entry_t;

/* Entry commit marker */
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
