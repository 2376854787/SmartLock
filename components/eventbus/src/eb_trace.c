#include "eb_trace.h"

#if (defined(EB_CFG_ENABLE_TRACE) && (EB_CFG_ENABLE_TRACE == 1))

#include <string.h>

#ifndef EB_TRACE_MAGIC
#define EB_TRACE_MAGIC (0x45525442u)
#endif

#if (defined(EB_CFG_TRACE_NOINIT) && (EB_CFG_TRACE_NOINIT == 1))
#define EB_NOINIT __attribute__((section(".noinit")))
#else
#define EB_NOINIT
#endif

static EB_NOINIT eb_trace_header_t g_hdr;
static EB_NOINIT eb_trace_entry_t g_entries[EB_TRACE_DEPTH];
/**
 * @brief 初始化日志头 以及日志条目存储空间非首次初始化
 */
static inline void hdr_sanitize(void) {
    if (g_hdr.magic != EB_TRACE_MAGIC || g_hdr.depth != EB_TRACE_DEPTH ||
        g_hdr.version != 0x0001u) {
        /* 首次或内存不可信 初始化 */
        memset(&g_hdr, 0, sizeof(g_hdr));
        g_hdr.magic     = EB_TRACE_MAGIC;
        g_hdr.version   = 0x0001u;
        g_hdr.depth     = EB_TRACE_DEPTH;
        g_hdr.write_idx = 0u;
        g_hdr.wrap_cnt  = 0u;
        /* entries 不强制清零 */
        memset(g_entries, 0, sizeof(g_entries));
    }
}
/**
 * @brief 初始化日志头 以及复位原因
 */
void eb_trace_init(void) {
    hdr_sanitize();
    g_hdr.reset_reason = (uint16_t)eb_port_read_reset_reason_and_clear();
}
/**
 * @brief 清理事件总线的追踪信息
 */
void eb_trace_clear(void) {
    memset(&g_hdr, 0, sizeof(g_hdr));
    g_hdr.magic        = EB_TRACE_MAGIC;
    g_hdr.version      = 0x0001u;
    g_hdr.depth        = EB_TRACE_DEPTH;
    g_hdr.reset_reason = (uint16_t)eb_port_read_reset_reason_and_clear();
    memset(g_entries, 0, sizeof(g_entries));
}
/**
 * @brief 将当前阶段信息存储到黑匣子
 * @param phase 阶段
 * @param ev 事件
 * @param reason 丢弃原因
 */
void eb_trace_record(eb_trace_phase_t phase, const eb_event_t* ev, eb_drop_reason_t reason) {
    if (!ev) return;
    /* 验证header有效性 没有初始化或者被破坏 */
    if (g_hdr.magic != EB_TRACE_MAGIC || g_hdr.depth != EB_TRACE_DEPTH ||
        g_hdr.version != 0x0001u) {
        return;
    }
    if (EB_TRACE_DEPTH == 0u) {
        return;
    }

    /* 多生产者 预定 */
    const uint32_t w    = __atomic_fetch_add(&g_hdr.write_idx, 1u, __ATOMIC_RELAXED);
    const uint32_t idx  = w % (uint32_t)EB_TRACE_DEPTH;
    eb_trace_entry_t* e = &g_entries[idx];
    /* 标记该信息还没提交 */
    __atomic_store_n(&e->commit, 0u, __ATOMIC_RELAXED);
    /* 写入数据 */
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
    /* 确保前面的代码执行完再进行下一步 */
    __atomic_thread_fence(__ATOMIC_RELEASE);
    /* 记录序列号 */
    e->seq = w;
    __atomic_store_n(&e->commit, (uint32_t)EB_TRACE_COMMIT_MAGIC, __ATOMIC_RELEASE);

    /* 判断是否写满了 */
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

