#include "eb_api.h"
#include "eb_config.h"
#include "eb_eventdef.h"
#include "eb_port.h"
#include "eb_sub.h"

#if EB_ENABLE_ASSERT
#define EB_ASSERT(x)                          \
    do {                                      \
        if (!(x)) eb_port_panic("EB_ASSERT"); \
    } while (0)
#else
#define EB_ASSERT(x) \
    do {             \
    } while (0)
#endif

typedef struct {
    eb_event_t* buf;
    uint16_t cap;
    uint16_t head; /* write */
    uint16_t tail; /* read  */
    uint16_t len;
} eb_queue_t;

/* 静态存储：不动态分配 */
static eb_event_t qh_buf[EB_QDEPTH_H];
static eb_event_t qm_buf[EB_QDEPTH_M];
static eb_event_t ql_buf[EB_QDEPTH_L];

static eb_queue_t qh, qm, ql;
static eb_stats_t g_stats;

/**
 * @brief 初始化队列
 * @param q 队列
 * @param buf 事件
 * @param cap 队列容量
 */
static void q_init(eb_queue_t* q, eb_event_t* buf, uint16_t cap) {
    q->buf  = buf;
    q->cap  = cap;
    q->head = q->tail = q->len = 0;
}
/**
 *
 * @param q 队列
 * @param ev 事件
 * @return 返回执行结果
 */
static bool q_push(eb_queue_t* q, const eb_event_t* ev) {
    if (q->len >= q->cap) return false;
    q->buf[q->head] = *ev;
    q->head         = (uint16_t)((q->head + 1u) % q->cap);
    q->len++;
    return true;
}
/**
 * @brief 从指定队列中取出一个事件
 * @param q 队列
 * @param out 输出事件
 * @return 执行结果
 */
static bool q_pop(eb_queue_t* q, eb_event_t* out) {
    if (q->len == 0u) return false;
    *out    = q->buf[q->tail];
    q->tail = (uint16_t)((q->tail + 1u) % q->cap);
    q->len--;
    return true;
}

/**
 * @brief 同一事件覆盖负载数据
 * @param q 队列
 * @param ev 事件
 * @return
 */
static bool q_overwrite_if_exists(eb_queue_t* q, const eb_event_t* ev) {
    /* 扫描 len 个元素，从 tail 开始 */
    uint16_t idx = q->tail;
    for (uint16_t k = 0; k < q->len; k++) {
        eb_event_t* slot = &q->buf[idx];
        if (slot->event_id == ev->event_id && slot->key == ev->key) {
            /* 覆盖最新状态 */
            slot->payload_u32 = ev->payload_u32;
            slot->ts          = ev->ts;
            return true;
        }
        idx = (uint16_t)((idx + 1u) % q->cap);
    }
    return false;
}

/**
 * @brief 初始化所以所有优先级的队列以及全局状态
 */
void eb_init(void) {
    q_init(&qh, qh_buf, (uint16_t)EB_QDEPTH_H);
    q_init(&qm, qm_buf, (uint16_t)EB_QDEPTH_M);
    q_init(&ql, ql_buf, (uint16_t)EB_QDEPTH_L);
    g_stats = (eb_stats_t){0};
    eb_sub_init(); /* 订阅表初始化 */
}
/**
 * @brief 返回指定优先级队列的指针
 * @param p 队列
 * @return 队列指针
 */
static eb_queue_t* pick_q(eb_prio_t p) {
    switch (p) {
        case EB_PRIO_H:
            return &qh;
        case EB_PRIO_M:
            return &qm;
        default:
            return &ql;
    }
}
/**
 * @brief 把事件发布到指定优先级队列
 * @param ev_in 事件
 * @return 执行结果
 */
eb_ret_t eb_publish(const eb_event_t* ev_in) {
    if (!ev_in) return EB_ERR_BADARG;
    const eb_eventdef_t* def = eb_eventdef_get(ev_in->event_id);
    if (!def) return EB_ERR_BADARG;                        /* 未定义事件：拒绝 */
    if (def->plane == EB_PLANE_DATA) return EB_ERR_BADARG; /* Data 禁止进总线 */
    eb_event_t ev = *ev_in;
    ev.ts         = eb_port_timestamp();
    eb_queue_t* q = pick_q(ev.prio);
    /* 统计：你现在把 enq 当“发布尝试次数”也可以（保留你原逻辑） */
    if (ev.prio == EB_PRIO_H)
        g_stats.enq_h++;
    else if (ev.prio == EB_PRIO_M)
        g_stats.enq_m++;
    else
        g_stats.enq_l++;
    const uint32_t pm = eb_port_enter_critical();
    bool ok           = q_push(q, &ev);
    if (!ok) {
        /* 只有 Snapshot 才允许覆盖/合并 */
        if (def->semantic == EB_SEM_SNAPSHOT &&
            (def->drop_policy == EB_OVERWRITE || def->drop_policy == EB_COALESCE_LATEST)) {
            ok = q_overwrite_if_exists(q, &ev);
            if (ok) {
                eb_port_exit_critical(pm);
                return EB_OK; /* 覆盖成功：不算 drop */
            }
        }
        /* 覆盖失败 / Edge：DROP_NEW */
        eb_port_exit_critical(pm);
        if (ev.prio == EB_PRIO_H)
            g_stats.drop_h++;
        else if (ev.prio == EB_PRIO_M)
            g_stats.drop_m++;
        else
            g_stats.drop_l++;

        return EB_ERR_FULL;
    }
    eb_port_exit_critical(pm);
    return EB_OK;
}

/**
 * @brief 查询返回当前的事件总线状态
 * @return 状态结构体
 */
const eb_stats_t* eb_get_stats(void) {
    return &g_stats;
}

/* BusTask：先实现“按策略出队 + 计数”，订阅/过滤/投递下一步做 */
void eb_pump_once(void) {
    eb_event_t ev;
    /* 1) 先尽可能处理 H */
    while (1) {
        const uint32_t pm = eb_port_enter_critical();
        const bool ok     = q_pop(&qh, &ev);
        eb_port_exit_critical(pm);
        if (!ok) break;
        g_stats.deq_h++;
        /* TODO(next): 查订阅表 -> filter -> 投递 -> trace */
        eb_dispatch(&ev);
    }

    /* 2) 再处理 M */
    while (1) {
        const uint32_t pm = eb_port_enter_critical();
        const bool ok     = q_pop(&qm, &ev);
        eb_port_exit_critical(pm);
        if (!ok) break;
        g_stats.deq_m++;
        /* TODO(next) */
        eb_dispatch(&ev);
    }

    /* 3) L 做配额 */
    for (uint32_t i = 0; i < EB_L_QUOTA_PER_ROUND; i++) {
        const uint32_t pm = eb_port_enter_critical();
        const bool ok     = q_pop(&ql, &ev);
        eb_port_exit_critical(pm);
        if (!ok) break;
        g_stats.deq_l++;
        /* TODO(next) */
        eb_dispatch(&ev);
    }
}
