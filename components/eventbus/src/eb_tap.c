#include "eb_tap.h"

#if (EB_CFG_ENABLE_TAP == 1)

#include <string.h>

static eb_tap_cfg_t g_cfg;
static eb_tap_sink_fn g_sink;
static uint32_t g_sample_ctr;

static inline bool pass_filter(const eb_event_t* ev) {
    if (!ev) return false;
    if ((ev->key & g_cfg.key_mask) != g_cfg.key_value) return false;
    return true;
}

static inline bool pass_sample(void) {
    const uint16_t p = g_cfg.sample_pow2;
    if (p == 0) return true;
    /* 2^p 抽样：用计数器低位 */
    g_sample_ctr++;
    return ((g_sample_ctr & ((1u << p) - 1u)) == 0u);
}

void eb_tap_init(const eb_tap_cfg_t* cfg, eb_tap_sink_fn sink) {
    if (cfg) g_cfg = *cfg;
    else memset(&g_cfg, 0, sizeof(g_cfg));
    g_sink = sink;
    g_sample_ctr = 0;
}

void eb_tap_on_pub(const eb_event_t* ev) {
    if (!g_sink) return;
    if ((g_cfg.enable_mask & 0x1u) == 0u) return;
    if (!pass_filter(ev)) return;
    if (!pass_sample()) return;
    g_sink(ev, 0u, 0u);
}

void eb_tap_on_dispatch(const eb_event_t* ev) {
    if (!g_sink) return;
    if ((g_cfg.enable_mask & 0x2u) == 0u) return;
    if (!pass_filter(ev)) return;
    if (!pass_sample()) return;
    g_sink(ev, 1u, 0u);
}

void eb_tap_on_drop(const eb_event_t* ev, uint8_t drop_reason) {
    if (!g_sink) return;
    if ((g_cfg.enable_mask & 0x4u) == 0u) return;
    if (!pass_filter(ev)) return;
    if (!pass_sample()) return;
    g_sink(ev, 2u, drop_reason);
}

#endif
