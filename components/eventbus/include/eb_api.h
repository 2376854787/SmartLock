#ifndef SMARTLOCK_EB_API_H
#define SMARTLOCK_EB_API_H
#include "eb_types.h"

/* 初始化：清空队列与计数器（并初始化 Trace/Storm/Budget） */
void eb_init(void);

/* 发布：多生产者；先用临界区实现正确性 */
eb_ret_t eb_publish(const eb_event_t* ev);

/* BusTask 每次调用处理一批事件（按 H/M/L + L配额） */
void eb_pump_once(void);

/* ================= Typed API（类型安全快捷发布/订阅，消除手动强转） ================= */

/**
 * @brief 类型安全发布宏：自动构造 eb_event_t 并填充 type_tag。
 * @return eb_ret_t
 *
 * 用法示例：
 *   eb_ret_t rc = eb_publish_typed(EB_EVT_BH1750_READY, 0, lux_val, 0, 0x1001);
 */
#define eb_publish_typed(id, key, payload, source, ttag) \
    ({                                                   \
        const eb_event_t _ev_ = {                        \
            .event_id    = (id),                         \
            .prio        = 0, /* 被 eventdef 强制覆盖 */ \
            .key         = (key),                        \
            .payload_u32 = (payload),                    \
            .source_id   = (source),                     \
            .type_tag    = (ttag),                       \
        };                                               \
        eb_publish(&_ev_);                               \
    })

/* ================= 统计（基础） ================= */
typedef struct {
    /* publish */
    uint32_t pub_total; /* 发布事件数 */
    /* enqueue */
    uint32_t enq_h, enq_m, enq_l;          /* 事件尝试入队记录 */
    uint32_t drop_q_h, drop_q_m, drop_q_l; /* Bus队列满丢弃 */
    uint32_t ow_q_hit;                     /* Bus队列 overwrite 命中 */
    /* dispatch */
    uint32_t deq_h, deq_m, deq_l; /* 分发出列记录 */
    uint32_t no_sub;              /* 无订阅者 */
    uint32_t filt_drop;           /* 被过滤掉（mask/value） */
    /* mailbox */
    uint32_t mb_ok;        /* mailbox push 成功 */
    uint32_t mb_full_drop; /* mailbox 满且丢弃 */
    uint32_t mb_ow_hit;    /* mailbox overwrite 命中 */
    /* callback */
    uint32_t cb_called; /* callback 被调用次数 */
    /* storm */
    uint32_t storm_drop; /* 被 storm_policy 限频丢弃 */
    /* type */
    uint32_t type_mismatch; /* 类型指纹不匹配 */
} eb_stats_t;

typedef enum {
    PUB_TOTAL = 0,
    ENQ_H,
    ENQ_M,
    ENQ_L,
    DROP_Q_H,
    DROP_Q_M,
    DROP_Q_L,
    OW_Q_HIT,
    DEQ_H,
    DEQ_M,
    DEQ_L,
    NO_SUB,
    FILT_DROP,
    MB_OK,
    MB_FULL_DROP,
    MB_OW_HIT,
    CB_CALLED,
    STORM_DROP,
    TYPE_MISMATCH,
} eb_stats_event_t;

const eb_stats_t* eb_get_stats(void);
void eb_dispatch(const eb_event_t* ev);
void eb_state_update(eb_stats_event_t ev);

/* ================= Typed Subscribe 宏 ================= */
#include "eb_sub.h"

/**
 * @brief 类型安全订阅宏：自动构造 eb_sub_t 并填充 expected_type_tag。
 * @return eb_ret_t
 *
 * 用法示例：
 *   eb_ret_t rc = eb_subscribe_typed(
 *       EB_EVT_BH1750_READY, EB_DELIVERY_MAILBOX,
 *       NULL, &my_rb, NULL, 0, 0, 0x1001);
 */
#define eb_subscribe_typed(id, del, callback, mb, usr, kmask, kval, ttag) \
    ({                                                                    \
        const eb_sub_t _s_ = {                                            \
            .event_id          = (id),                                    \
            .delivery          = (del),                                   \
            .cb                = (callback),                              \
            .mailbox           = (mb),                                    \
            .user              = (usr),                                   \
            .key_mask          = (kmask),                                 \
            .key_value         = (kval),                                  \
            .expected_type_tag = (ttag),                                  \
        };                                                                \
        eb_sub_add(&_s_);                                                 \
    })

/* ================= Top-Tier 扩展 API ================= */
#include "eb_config.h"

#if (defined(EB_CFG_ENABLE_TRACE) && (EB_CFG_ENABLE_TRACE == 1))
#include "eb_trace.h"
#endif

#if (defined(EB_CFG_ENABLE_BUDGET) && (EB_CFG_ENABLE_BUDGET == 1))
#include "eb_budget.h"
#endif

#if (defined(EB_CFG_ENABLE_TAP) && (EB_CFG_ENABLE_TAP == 1))
#include "eb_tap.h"
#endif

#endif  // SMARTLOCK_EB_API_H

