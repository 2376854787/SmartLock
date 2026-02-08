#include "eb_sub.h"

#include "eb_port.h"
/* 事件订阅信息 */
static eb_sub_t g_subs[EB_MAX_SUBS];
/* 事件是否被占用 */
static uint8_t g_used[EB_MAX_SUBS];

void eb_sub_init(void) {
    for (uint32_t i = 0; i < EB_MAX_SUBS; i++) g_used[i] = 0;
}

/**
 * @brief 添加订阅信息
 * @param s 事件订阅信息
 * @return 32位状态码
 */
eb_ret_t eb_sub_add(const eb_sub_t* s) {
    if (!s) return EB_ERR_BADARG;
    /* 进入临界区 */
    const uint32_t pm = eb_port_enter_critical();
    /* 遍历订阅信息组当前是否有信息 没有就插入 */
    for (uint32_t i = 0; i < EB_MAX_SUBS; i++) {
        if (!g_used[i]) {
            g_subs[i] = *s;
            g_used[i] = 1;
            eb_port_exit_critical(pm);
            return EB_OK;
        }
    }
    eb_port_exit_critical(pm);
    return EB_ERR_FULL;
}
/**
 * @brief 从订阅列表删除订阅信息
 * @param s 订阅信息
 * @return 32位状态码
 */
eb_ret_t eb_sub_remove(const eb_sub_t* s) {
    if (!s) return EB_ERR_BADARG;
    /* 进入临界区 */
    const uint32_t pm = eb_port_enter_critical();
    /* 遍历订阅信息表 找到全部信息满足的信息并删除 */
    for (uint32_t i = 0; i < EB_MAX_SUBS; i++) {
        if (g_used[i] && g_subs[i].event_id == s->event_id && g_subs[i].delivery == s->delivery &&
            g_subs[i].cb == s->cb && g_subs[i].mailbox == s->mailbox && g_subs[i].user == s->user) {
            g_used[i] = 0;
            eb_port_exit_critical(pm);
            return EB_OK;
        }
    }
    eb_port_exit_critical(pm);
    return EB_ERR_BADARG;
}
/**
 * @brief 找出对同一事件的订阅信息 存入指定的地址
 * @param event_id 事件id
 * @param out_list 输出列表  保存对同一事件订阅的信息
 * @param max 最大输出数量
 * @return
 */
uint32_t eb_sub_find(uint32_t event_id, const eb_sub_t** out_list, uint32_t max) {
    uint32_t n = 0;
    if (!out_list || max == 0u) return 0;
    const uint32_t pm = eb_port_enter_critical();
    for (uint32_t i = 0; i < EB_MAX_SUBS && n < max; i++) {
        if (g_used[i] && g_subs[i].event_id == event_id) {
            out_list[n++] = &g_subs[i];
        }
    }
    eb_port_exit_critical(pm);
    return n;
}
