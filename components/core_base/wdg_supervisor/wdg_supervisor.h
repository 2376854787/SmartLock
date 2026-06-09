#ifndef WDG_SUPERVISOR_H
#define WDG_SUPERVISOR_H
#include <stdbool.h>
#include <stdint.h>

#include "ret_code_t.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WDG_SUP_MAX_WATCH 32u
#define WDG_SUP_ID_SYSTEM_HEARTBEAT 0xFFu
#define WDG_SUP_ID_WDG_KICK         0xFEu

/* 监督对象类型 */
typedef enum {
    WDG_WATCH_HEARTBEAT = 0, /* 仅心跳 */
    WDG_WATCH_CHALLENGE = 1, /* 挑战应答 */
} wdg_watch_type_t;

/* 挑战算法类型 */
typedef enum {
    WDG_ALGO_MATH_MIX32 = 0,
} wdg_algo_t;

/* 看门狗失败原因（用于日志和 blackbox） */
typedef enum {
    WDG_SUP_FAIL_TIMEOUT      = 1, /* 超时 */
    WDG_SUP_FAIL_WRONG_ANSWER = 2, /* 答案错误 */
    WDG_SUP_FAIL_HB_MISSING   = 3, /* 心跳缺失 */
    WDG_SUP_FAIL_FRESH        = 4, /* 喂狗失败 */
} wdg_sup_fail_reason_t;

/* 监督器运行状态机 */
typedef enum {
    WDG_SUP_STATE_INIT = 0,         /* 已初始化，尚未进入稳定运行 */
    WDG_SUP_STATE_WARMUP = 1,       /* 启动宽限期：仅保活，不做 challenge */
    WDG_SUP_STATE_RUN = 2,          /* 稳态运行：心跳 + challenge 严格检查 */
    WDG_SUP_STATE_FAIL_LATCHED = 3, /* 已锁存失败，等待硬件复位 */
} wdg_sup_state_t;

/* 初始化：
 * - period_ms：Supervisor 周期
 * - boot_grace_ms：启动宽松窗口（例如 6000ms），窗口内只要 Supervisor 活着就允许喂狗
 */
ret_code_t wdg_sup_init(uint32_t period_ms, uint32_t boot_grace_ms);

/* 注册被监督对象：
 * - type=HEARTBEAT：模块只需周期调用 wdg_sup_heartbeat(id)
 * - type=CHALLENGE：关键任务必须周期调用 wdg_sup_task_service(id) 来应答挑战
 *
 * key/param/deadline_ms 仅对 CHALLENGE 有效：
 * - key：每任务私有 key（编译期常量即可）
 * - param：算法参数（迭代次数等，控制计算耗时）
 * - deadline_ms：从发出挑战开始必须在此时间内应答
 */
ret_code_t wdg_sup_register(uint8_t *out_id, const char *name, wdg_watch_type_t type,
                            wdg_algo_t algo, uint32_t key, uint32_t param, uint32_t deadline_ms);

/* HEARTBEAT：模块周期调用 */
ret_code_t wdg_sup_heartbeat(uint8_t id);

/* HEARTBEAT 窗口：允许缺失 miss_budget_cycles 个周期后再判死（>=1） */
ret_code_t wdg_sup_set_hb_miss_budget(uint8_t id, uint32_t miss_budget_cycles);
/* 获取 HEARTBEAT 允许缺失的周期窗口；非法参数返回0 */
uint32_t wdg_sup_get_hb_miss_budget(uint8_t id);

/* CHALLENGE：关键任务周期调用，收到挑战就计算并回写应答 */
ret_code_t wdg_sup_task_service(uint8_t id);

/* 运行方式二选一：
 * - RTOS：创建 Supervisor 线程
 * - 裸机：主循环周期调用 wdg_sup_poll()
 */
ret_code_t wdg_sup_start(void);
ret_code_t wdg_sup_poll(void);

/* 参数预算辅助：deadline = service + jitter + compute + margin（毫秒） */
uint32_t wdg_sup_deadline_budget_ms(uint32_t service_ms, uint32_t jitter_ms, uint32_t compute_ms,
                                    uint32_t margin_ms);

/* 调试辅助：按 id 获取监控对象名称 */
const char *wdg_sup_get_name(uint8_t id);
/* 调试辅助：失败原因码转字符串 */
const char *wdg_sup_reason_str(uint32_t reason);
/* 调试辅助：获取当前监督器状态枚举 */
wdg_sup_state_t wdg_sup_get_state(void);
/* 调试辅助：获取当前监督器状态字符串 */
const char *wdg_sup_get_state_name(void);

/* 失败落盘 Hook（可选：你可以在别的 .c 里实现它调用 blackbox） */
void wdg_sup_fail_hook(uint8_t id, uint32_t seq, uint32_t nonce, uint32_t expected, uint32_t got,
                       uint32_t reason);

#ifdef __cplusplus
}
#endif

#endif
