#include "eb_api.h"
#include "eb_port.h"
#include "eb_sub.h"
/*　一次最大分发数　*/
#ifndef EB_MAX_MATCH
#define EB_MAX_MATCH 16u
#endif
extern eb_stats_t
    g_stats_internal; /* 你可以在 eb_core.c 提供内部访问函数，或把 stats 放到一个模块内 */

/**
 * @brief 对订阅信息和事件进行过滤
 * @param ev 事件
 * @param s 订阅信息
 * @return 返回通过筛选的订阅当前事件的订阅信息
 */
static inline bool eb_filter_hit(const eb_event_t* ev, const eb_sub_t* s) {
    if (s->key_mask == 0u) return true;
    return ((ev->key & s->key_mask) == s->key_value);
}

/**
 * @brief 分发一个事件
 * @param ev 事件
 * @param s 订阅信息
 * @return
 */
static eb_ret_t dispatch_one(const eb_event_t* ev, const eb_sub_t* s) {
    /* 投递方式为 执行回调 */
    if (s->delivery == EB_DELIVERY_CALLBACK) {
        if (s->cb) s->cb(ev, s->user);
        return EB_OK;
    } else { /* 执行投递到邮箱 */
        if (!s->mailbox) return EB_ERR_BADARG;
        return eb_port_mailbox_push(s->mailbox, ev) ? EB_OK : EB_ERR_FULL;
    }
}
/**
 * @brief 对一个事件的所有订阅者进行分发
 * @param ev 事件
 */
void eb_dispatch(const eb_event_t* ev) {
    const eb_sub_t* list[EB_MAX_MATCH];
    const uint32_t n = eb_sub_find(ev->event_id, list, EB_MAX_MATCH);
    /* 没订阅者：允许丢弃，但要计数（后续能用来查“发布了没人收”） */
    if (n == 0u) {
        /* TODO: g_stats.no_sub++ */
        return;
    }
    for (uint32_t i = 0; i < n; i++) {
        /* 过滤掉 错误的订阅信息 */
        if (!eb_filter_hit(ev, list[i])) {
            continue;
        }
        const eb_ret_t r = dispatch_one(ev, list[i]);
        (void)r;
        /* TODO: 成功/失败计数 */
    }
}
