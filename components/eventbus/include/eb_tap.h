#ifndef EB_TAP_H
#define EB_TAP_H

#include <stdint.h>
#include <stdbool.h>
#include "eb_config.h"
#include "eb_types.h"

#if (EB_CFG_ENABLE_TAP == 1)

/* Tap 目的：旁路监听，不走 eventbus queue，不触发 storm/budget（避免自激）
 * - O(1) 执行：写一个小 ring（由 port 提供）或调用一个轻量回调
 * - 可配置采样率与过滤掩码
 */

typedef struct {
    uint32_t enable_mask;      /* bit0: publish, bit1: dispatch, bit2: drop */
    uint32_t key_mask;
    uint32_t key_value;
    uint16_t sample_pow2;      /* 0=全采样；1=1/2；2=1/4... 采样 */
    uint16_t _pad;
} eb_tap_cfg_t;

/* 由 port 层实现：将 tap 记录输出到 RTT/ITM/自建 ringbuffer/共享内存 */
typedef void (*eb_tap_sink_fn)(const eb_event_t* ev, uint8_t phase, uint8_t drop_reason);

void eb_tap_init(const eb_tap_cfg_t* cfg, eb_tap_sink_fn sink);

/* phase: 0=pub, 1=dispatch, 2=drop；drop_reason 复用你的 reason 枚举 */
void eb_tap_on_pub(const eb_event_t* ev);
void eb_tap_on_dispatch(const eb_event_t* ev);
void eb_tap_on_drop(const eb_event_t* ev, uint8_t drop_reason);

#endif /* EB_CFG_ENABLE_TAP */
#endif /* EB_TAP_H */
