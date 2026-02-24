#include "eb_tap.h"

#if (defined(EB_CFG_ENABLE_TAP) && (EB_CFG_ENABLE_TAP == 1))

#include <string.h>

#include "assert_cus.h"

#if EB_ENABLE_ASSERT
#define EB_ASSERT_PARAM(x) ASSERT_PARAM((x))
#else
#define EB_ASSERT_PARAM(x) \
    do {                   \
        (void)sizeof(x);   \
    } while (0)
#endif

static eb_tap_cfg_t g_cfg;
static eb_tap_sink_fn g_sink;
static uint32_t g_sample_ctr;

/**
 * @brief 过滤掉非目标的事件
 * @param ev 事件
 * @return是否通过筛选
 */
static inline bool pass_filter(const eb_event_t* ev) {
    if (!ev) return false;
    if ((ev->key & g_cfg.key_mask) != g_cfg.key_value) return false;
    return true;
}
/**
 * @brief 控制采样频率
 * @return 判断当前这个时刻是否是可以采样的
 */
static inline bool pass_sample(void) {
    const uint16_t p = g_cfg.sample_pow2;
    if (p == 0) return true;
    g_sample_ctr++;
    return ((g_sample_ctr & ((1u << p) - 1u)) == 0u);
}

/**
 * @brief 配置tap 抓包的配置和回调函数
 * @param cfg 配置
 * @param sink 回调函数
 */
void eb_tap_init(const eb_tap_cfg_t* cfg, eb_tap_sink_fn sink) {
    if (cfg)
        g_cfg = *cfg;
    else
        memset(&g_cfg, 0, sizeof(g_cfg));
    g_sink       = sink;
    g_sample_ctr = 0;
}
/**
 *  @brief 发布时采样
 * @param ev 事件
 */
void eb_tap_on_pub(const eb_event_t* ev) {
    EB_ASSERT_PARAM(ev != NULL);
    if (ev == NULL) return;
    if (!g_sink) return;
    if ((g_cfg.enable_mask & 0x1u) == 0u) return;
    if (!pass_filter(ev)) return;
    if (!pass_sample()) return;
    g_sink(ev, 0u, 0u);
}
/**
 * @brief 分发时采样
 * @param ev 事件
 */
void eb_tap_on_dispatch(const eb_event_t* ev) {
    EB_ASSERT_PARAM(ev != NULL);
    if (ev == NULL) return;
    if (!g_sink) return;
    if ((g_cfg.enable_mask & 0x2u) == 0u) return;
    if (!pass_filter(ev)) return;
    if (!pass_sample()) return;
    g_sink(ev, 1u, 0u);
}
/**
 * @brief 丢弃时采样
 * @param ev 事件
 */
void eb_tap_on_drop(const eb_event_t* ev, uint8_t drop_reason) {
    EB_ASSERT_PARAM(ev != NULL);
    if (ev == NULL) return;
    if (!g_sink) return;
    if ((g_cfg.enable_mask & 0x4u) == 0u) return;
    if (!pass_filter(ev)) return;
    if (!pass_sample()) return;
    g_sink(ev, 2u, drop_reason);
}

#endif

