#ifndef EB_TAP_H
#define EB_TAP_H

#include <stdbool.h>
#include <stdint.h>

#include "eb_config.h"
#include "eb_types.h"

#if (defined(EB_CFG_ENABLE_TAP) && (EB_CFG_ENABLE_TAP == 1))


typedef struct {
    uint32_t enable_mask; /* Bit0发布时监听 Bit1:分发时监听 Bit2：丢弃时监听 */
    uint32_t key_mask;    /* 过滤掩码 */
    uint32_t key_value;   /* 过滤目标 */
    uint16_t sample_pow2; /* 降低采样率 1/2^n次 分频采样*/
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

