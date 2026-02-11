#include "eb_trace.h"

#if (EB_CFG_ENABLE_TRACE == 1)

#include <string.h>

/* GCC/Clang builtins are used to avoid requiring <stdatomic.h> on bare-metal */

#ifndef EB_TRACE_MAGIC
#define EB_TRACE_MAGIC (0x45525442u) /* 'EBTR' little-endian */
#endif

#if (EB_CFG_TRACE_NOINIT == 1)
#define EB_NOINIT __attribute__((section(".noinit")))
#else
#define EB_NOINIT
#endif

static EB_NOINIT eb_trace_header_t g_hdr;
static EB_NOINIT eb_trace_entry_t g_entries[EB_TRACE_DEPTH];

static inline void hdr_sanitize(void) {
    if (g_hdr.magic != EB_TRACE_MAGIC || g_hdr.depth != EB_TRACE_DEPTH || g_hdr.version != 0x0001u) {
        /* 首次或内存不可信：初始化 */
        memset(&g_hdr, 0, sizeof(g_hdr));
        g_hdr.magic   = EB_TRACE_MAGIC;
        g_hdr.version = 0x0001u;
        g_hdr.depth   = EB_TRACE_DEPTH;
        g_hdr.write_idx = 0u;
        g_hdr.wrap_cnt  = 0u;
        /* entries 不强制清零（noinit）——但我们初始化时清零一次避免脏数据 */
        memset(g_entries, 0, sizeof(g_entries));
    }
}

void eb_trace_init(void) {
    hdr_sanitize();
    g_hdr.reset_reason = (uint16_t)eb_port_read_reset_reason_and_clear();
}

void eb_trace_clear(void) {
    memset(&g_hdr, 0, sizeof(g_hdr));
    g_hdr.magic   = EB_TRACE_MAGIC;
    g_hdr.version = 0x0001u;
    g_hdr.depth   = EB_TRACE_DEPTH;
    g_hdr.reset_reason = (uint16_t)eb_port_read_reset_reason_and_clear();
    memset(g_entries, 0, sizeof(g_entries));
}

void eb_trace_record(eb_trace_phase_t phase, const eb_event_t* ev, eb_drop_reason_t reason) {
    if (!ev) return;

    /* Fast-path: require eb_trace_init() to have run before interrupts/tasks.
     * If header looks invalid, skip recording (best-effort). */
    if (g_hdr.magic != EB_TRACE_MAGIC || g_hdr.depth != EB_TRACE_DEPTH || g_hdr.version != 0x0001u) {
        return;
    }
    if (EB_TRACE_DEPTH == 0u) {
        return;
    }

    /* Multi-producer reservation: each caller gets a unique sequence number. */
    const uint32_t w = __atomic_fetch_add(&g_hdr.write_idx, 1u, __ATOMIC_RELAXED);
    const uint32_t idx = w % (uint32_t)EB_TRACE_DEPTH;
    eb_trace_entry_t* e = &g_entries[idx];

    /* Two-phase commit to avoid torn entries after crash/reset:
     * 1) clear commit marker
     * 2) write fields
     * 3) release fence
     * 4) publish seq + commit marker
     */
    __atomic_store_n(&e->commit, 0u, __ATOMIC_RELAXED);

    e->ts_us       = eb_port_timestamp_us();
    e->event_id    = ev->event_id;
    e->key         = ev->key;
    e->payload_u32 = ev->payload_u32;
    e->source_id   = ev->source_id;
    e->type_tag    = ev->type_tag;
    e->prio        = (uint8_t)ev->prio;
    e->phase       = (uint8_t)phase;
    e->drop_reason = (uint8_t)reason;
    e->reserved    = 0u;

    __atomic_thread_fence(__ATOMIC_RELEASE);
    e->seq = w;
    __atomic_store_n(&e->commit, (uint32_t)EB_TRACE_COMMIT_MAGIC, __ATOMIC_RELEASE);

    /* wrap_cnt is informational; keep it approximately correct under concurrency */
    if (((w + 1u) % (uint32_t)EB_TRACE_DEPTH) == 0u) {
        (void)__atomic_fetch_add(&g_hdr.wrap_cnt, 1u, __ATOMIC_RELAXED);
    }
}

const eb_trace_header_t* eb_trace_header(void) {
    hdr_sanitize();
    return &g_hdr;
}

const eb_trace_entry_t* eb_trace_entries(void) {
    return g_entries;
}

#endif /* EB_CFG_ENABLE_TRACE */
