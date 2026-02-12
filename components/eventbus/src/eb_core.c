#include <stddef.h>

#include "assert_cus.h"
#include "eb_api.h"
#include "eb_config.h"
#include "eb_eventdef.h"
#include "eb_port.h"
#include "eb_storm.h"
#include "eb_sub.h"

#if (EB_CFG_ENABLE_TAP == 1)
#include "eb_tap.h"
#endif

#if (EB_CFG_ENABLE_BUDGET == 1)
#include "eb_budget.h"
#endif

#if (EB_CFG_ENABLE_TRACE == 1)
#include "eb_trace.h"
#endif

#if EB_ENABLE_ASSERT
#define EB_ASSERT(x) ASSERT_FATAL((x))
#else
#define EB_ASSERT(x) \
    do {             \
    } while (0)
#endif

/* 丢弃原因与 eb_trace 的 reason 编码保持一致（即使关闭 Trace 也稳定） */
#define EB_TAP_DROP_NONE      (0u)
#define EB_TAP_DROP_BUSQ_FULL (1u)
#define EB_TAP_DROP_MB_FULL   (2u)
#define EB_TAP_DROP_STORM     (3u)
#define EB_TAP_DROP_TYPE      (4u)

static inline void eb_stats_inc(volatile uint32_t* p) {
    (void)__atomic_fetch_add((uint32_t*)p, 1u, __ATOMIC_RELAXED);
}

typedef struct {
    eb_event_t* buf; /* 存储地址 */
    uint16_t cap;    /* 容量 */
    uint16_t head;   /* 写指针 */
    uint16_t tail;   /* 读指针  */
    uint16_t len;    /* 当前长度 */
} eb_queue_t;

/* 静态存储 */
static eb_event_t qh_buf[EB_QDEPTH_H];
static eb_event_t qm_buf[EB_QDEPTH_M];
static eb_event_t ql_buf[EB_QDEPTH_L];

static eb_queue_t qh, qm, ql;
static eb_stats_t g_stats;
/**
 * @brief 触发事件就将某个变量原子增加
 * @param ev 事件状态
 */
void eb_state_update(eb_stats_event_t ev) {
    switch (ev) {
        case PUB_TOTAL:
            eb_stats_inc(&g_stats.pub_total);
            break;
        case ENQ_H:
            eb_stats_inc(&g_stats.enq_h);
            break;
        case ENQ_M:
            eb_stats_inc(&g_stats.enq_m);
            break;
        case ENQ_L:
            eb_stats_inc(&g_stats.enq_l);
            break;
        case DROP_Q_H:
            eb_stats_inc(&g_stats.drop_q_h);
            break;
        case DROP_Q_M:
            eb_stats_inc(&g_stats.drop_q_m);
            break;
        case DROP_Q_L:
            eb_stats_inc(&g_stats.drop_q_l);
            break;
        case OW_Q_HIT:
            eb_stats_inc(&g_stats.ow_q_hit);
            break;
        case DEQ_H:
            eb_stats_inc(&g_stats.deq_h);
            break;
        case DEQ_M:
            eb_stats_inc(&g_stats.deq_m);
            break;
        case DEQ_L:
            eb_stats_inc(&g_stats.deq_l);
            break;
        case NO_SUB:
            eb_stats_inc(&g_stats.no_sub);
            break;
        case FILT_DROP:
            eb_stats_inc(&g_stats.filt_drop);
            break;
        case MB_OK:
            eb_stats_inc(&g_stats.mb_ok);
            break;
        case MB_FULL_DROP:
            eb_stats_inc(&g_stats.mb_full_drop);
            break;
        case MB_OW_HIT:
            eb_stats_inc(&g_stats.mb_ow_hit);
            break;
        case CB_CALLED:
            eb_stats_inc(&g_stats.cb_called);
            break;
        case STORM_DROP:
            eb_stats_inc(&g_stats.storm_drop);
            break;
        case TYPE_MISMATCH:
            eb_stats_inc(&g_stats.type_mismatch);
            break;
        default:
            break;
    }
}
/**
 * @brief 初始化队列
 * @param q 队列
 * @param buf 缓冲区
 * @param cap 容量
 */
static void q_init(eb_queue_t* q, eb_event_t* buf, uint16_t cap) {
    q->buf  = buf;
    q->cap  = cap;
    q->head = 0u;
    q->tail = 0u;
    q->len  = 0u;
}
/**
 * @brief 将事件存储到队列
 * @param q 队列
 * @param ev 事件
 * @return 是否入队成功
 */
static bool q_push(eb_queue_t* q, const eb_event_t* ev) {
    if (q->len >= q->cap) return false;
    q->buf[q->head] = *ev;
    q->head         = (uint16_t)((q->head + 1u) % q->cap);
    q->len++;
    return true;
}
/**
 * @brief 将队列出队一个事件
 * @param q 队列
 * @param out 事件接受地址
 * @return 是否出队成功
 */
static bool q_pop(eb_queue_t* q, eb_event_t* out) {
    if (q->len == 0u) return false;
    *out    = q->buf[q->tail];
    q->tail = (uint16_t)((q->tail + 1u) % q->cap);
    q->len--;
    return true;
}

/**
 * @brief 队列满的时候将最老的丢弃入队新的事件
 * @param q 队列
 * @param ev 事件
 * @return
 */
static bool q_overwrite_drop_oldest(eb_queue_t* q, const eb_event_t* ev) {
    if (q->cap == 0u) return false;
    /* 正常入队 */
    if (q->len < q->cap) {
        return q_push(q, ev);
    }

    /* 覆盖head 位置 */
    q->buf[q->head] = *ev;
    /* +1 */
    q->head         = (uint16_t)((q->head + 1u) % q->cap);
    q->tail         = q->head;
    q->len          = q->cap;
    return true;
}
/**
 * @brief 返回对应优先级队列的地址
 * @param p 优先级
 * @return 返回对应优先级队列的地址
 */
static eb_queue_t* pick_q(eb_prio_t p) {
    switch (p) {
        case EB_PRIO_H:
            return &qh;
        case EB_PRIO_M:
            return &qm;
        case EB_PRIO_L:
            return &ql;
        default:
            /* 未定义优先级 */
            EB_ASSERT(-1);
    }
    return NULL;
}
/**
 * @brief 初始化事件总线、队列、监察状态、哈希表
 */
void eb_init(void) {
    /* 初始化队列 */
    q_init(&qh, qh_buf, (uint16_t)EB_QDEPTH_H);
    q_init(&qm, qm_buf, (uint16_t)EB_QDEPTH_M);
    q_init(&ql, ql_buf, (uint16_t)EB_QDEPTH_L);
    g_stats = (eb_stats_t){0};
    /* 构建哈希查找表 */
    eb_eventdef_init();
    eb_sub_init();

#if (EB_CFG_ENABLE_STORM == 1)
    /* 启用风暴防护 限制投递频率 */
    eb_storm_init();
#endif
#if (EB_CFG_ENABLE_TRACE == 1)
    /* 启用追踪 */
    eb_trace_init();
#endif
#if (EB_CFG_ENABLE_BUDGET == 1)
    // TODO 添加注释
    eb_budget_init();
#endif
}

/**
 * @brief 多生产者发布事件
 * @param ev_in 事件变量地址
 * @return 32位状态码
 */
eb_ret_t eb_publish(const eb_event_t* ev_in) {
    if (!ev_in) {
        EB_ASSERT(ev_in != 0);
        return EB_ERR_BADARG;
    }
    /* 获取事件策略 */
    const eb_eventdef_t* def = eb_eventdef_get(ev_in->event_id);
    if (!def) {
        EB_ASSERT(def != 0);
        return EB_ERR_BADARG; /* 未定义事件：拒绝 */
    }
    if (def->plane == EB_PLANE_DATA) {
        return EB_ERR_BADARG; /* Data 禁止进总线 */
    }

    eb_event_t ev = *ev_in;
    /* 在策略表优先级生效时强制使用策略表的优先级 */
    if (def->prio >= 0) {
        ev.prio = def->prio;
    }
    ev.ts = eb_port_timestamp();

    /* 允许 eventdef 在没有填充类型时 赋默认 type_tag */
    if (ev.type_tag == 0u && def->type_tag != 0u) {
        ev.type_tag = def->type_tag;
    }

#if (EB_CFG_ENABLE_TAP == 1)
    eb_tap_on_pub(&ev);
#endif

#if (EB_CFG_ENABLE_STORM == 1)
    /* 检查投递频率 */
    if (!eb_storm_allow(def, ev.source_id, ev.ts)) {
        eb_state_update(STORM_DROP);
#if (EB_CFG_ENABLE_TRACE == 1)
        /* 记录 */
        eb_trace_record(EB_TRACE_STORM_DROP, &ev, (eb_drop_reason_t)EB_TAP_DROP_STORM);
#endif
#if (EB_CFG_ENABLE_TAP == 1)
        eb_tap_on_drop(&ev, (uint8_t)EB_TAP_DROP_STORM);
#endif
        return EB_OK; /* 限频丢弃视为成功 */
    }
#endif

#if (EB_CFG_ENABLE_TRACE == 1)
    eb_trace_record(EB_TRACE_PUB, &ev, (eb_drop_reason_t)EB_TAP_DROP_NONE);
#endif

    /* 获取到指定的队列 */
    eb_queue_t* q = pick_q(ev.prio);

    /* 对应优先级事件尝试入队 +1 */
    if (ev.prio == EB_PRIO_H)
        eb_state_update(ENQ_H);
    else if (ev.prio == EB_PRIO_M)
        eb_state_update(ENQ_M);
    else
        eb_state_update(ENQ_L);

    uint32_t pm;
    eb_port_enter_critical(&pm);
    /* 入队 */
    bool ok = q_push(q, &ev);
    if (!ok) {
        /* 只有 Snapshot 才允许覆盖/合并 */
        if (def->semantic == EB_SEM_SNAPSHOT &&
            (def->drop_policy == EB_OVERWRITE || def->drop_policy == EB_COALESCE_LATEST)) {
            ok = q_overwrite_drop_oldest(q, &ev);
            if (ok) {
                eb_port_exit_critical(pm);
                eb_state_update(OW_Q_HIT);
#if (EB_CFG_ENABLE_TRACE == 1)
                eb_trace_record(EB_TRACE_ENQ, &ev, (eb_drop_reason_t)EB_TAP_DROP_NONE);
#endif
                return EB_OK;
            }
        }
        eb_port_exit_critical(pm);
        /* 队列满丢弃事件 */
        if (ev.prio == EB_PRIO_H)
            eb_state_update(DROP_Q_H);
        else if (ev.prio == EB_PRIO_M)
            eb_state_update(DROP_Q_M);
        else
            eb_state_update(DROP_Q_L);

#if (EB_CFG_ENABLE_TRACE == 1)
        eb_trace_record(EB_TRACE_DROP, &ev, (eb_drop_reason_t)EB_TAP_DROP_BUSQ_FULL);
#endif
#if (EB_CFG_ENABLE_TAP == 1)
        eb_tap_on_drop(&ev, (uint8_t)EB_TAP_DROP_BUSQ_FULL);
#endif
        return EB_ERR_FULL;
    }

    eb_state_update(PUB_TOTAL);
    eb_port_exit_critical(pm);

#if (EB_CFG_ENABLE_TRACE == 1)
    eb_trace_record(EB_TRACE_ENQ, &ev, (eb_drop_reason_t)EB_TAP_DROP_NONE);
#endif
    return EB_OK;
}
/**
 * @brief 获取全局状态
 * @return 获取全局状态
 */
const eb_stats_t* eb_get_stats(void) {
    return &g_stats;
}
/**
 * @brief 处理队列里面的事件
 */
void eb_pump_once(void) {
#if (EB_CFG_ENABLE_BUDGET == 1)
    const uint32_t round_t0 = eb_port_timestamp_us();
#endif

    eb_event_t ev;

    /* 高级队列 */
    while (1) {
        uint32_t pm;
        eb_port_enter_critical(&pm);
        const bool ok = q_pop(&qh, &ev);
        eb_port_exit_critical(pm);
        if (!ok) break;

        /* 分发出列事件 +1 */
        eb_state_update(DEQ_H);
#if (EB_CFG_ENABLE_TRACE == 1)
        eb_trace_record(EB_TRACE_DEQ, &ev, (eb_drop_reason_t)EB_TAP_DROP_NONE);
#endif

#if (EB_CFG_ENABLE_BUDGET == 1)
        /* 记录分发事件 */
        const uint32_t t0 = eb_port_timestamp_us();
#endif
        /* 分发事件 */
        eb_dispatch(&ev);
#if (EB_CFG_ENABLE_BUDGET == 1)
        const uint32_t t1 = eb_port_timestamp_us();
        eb_budget_record_event(ev.prio, (uint32_t)(t1 - t0));
#endif
    }
    /* 中级队列 */
    while (1) {
        uint32_t pm;
        eb_port_enter_critical(&pm);
        const bool ok = q_pop(&qm, &ev);
        eb_port_exit_critical(pm);
        if (!ok) break;

        eb_state_update(DEQ_M);
#if (EB_CFG_ENABLE_TRACE == 1)
        eb_trace_record(EB_TRACE_DEQ, &ev, (eb_drop_reason_t)EB_TAP_DROP_NONE);
#endif

#if (EB_CFG_ENABLE_BUDGET == 1)
        const uint32_t t0 = eb_port_timestamp_us();
#endif
        eb_dispatch(&ev);
#if (EB_CFG_ENABLE_BUDGET == 1)
        const uint32_t t1 = eb_port_timestamp_us();
        eb_budget_record_event(ev.prio, (uint32_t)(t1 - t0));
#endif
    }

    /* 低优先级队列 */
    for (uint32_t i = 0; i < EB_L_QUOTA_PER_ROUND; i++) {
        uint32_t pm;
        eb_port_enter_critical(&pm);
        const bool ok = q_pop(&ql, &ev);
        eb_port_exit_critical(pm);
        if (!ok) break;

        eb_state_update(DEQ_L);
#if (EB_CFG_ENABLE_TRACE == 1)
        eb_trace_record(EB_TRACE_DEQ, &ev, (eb_drop_reason_t)EB_TAP_DROP_NONE);
#endif

#if (EB_CFG_ENABLE_BUDGET == 1)
        const uint32_t t0 = eb_port_timestamp_us();
#endif
        eb_dispatch(&ev);
#if (EB_CFG_ENABLE_BUDGET == 1)
        const uint32_t t1 = eb_port_timestamp_us();
        eb_budget_record_event(ev.prio, (uint32_t)(t1 - t0));
#endif
    }

#if (EB_CFG_ENABLE_BUDGET == 1)
    const uint32_t round_t1 = eb_port_timestamp_us();
    eb_budget_record_round((uint32_t)(round_t1 - round_t0));
#endif
}
