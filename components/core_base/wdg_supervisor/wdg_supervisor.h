#ifndef WDG_SUPERVISOR_H
#define WDG_SUPERVISOR_H
#include <stdbool.h>
#include <stdint.h>

#include "ret_code.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WDG_SUP_MAX_WATCH 32u
typedef enum {
    WDG_WATCH_HEARTBEAT = 0, /* 仅心跳 */
    WDG_WATCH_CHALLENGE = 1, /* 挑战应答（关键任务） */
} wdg_watch_type_t;

typedef enum {
    WDG_ALGO_MATH_MIX32 = 0,
} wdg_algo_t;

/* 初始化：
 * - period_ms：Supervisor 周期（例如 200ms）
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

/* CHALLENGE：关键任务周期调用（非阻塞），收到挑战就计算并回写应答 */
ret_code_t wdg_sup_task_service(uint8_t id);

/* 运行方式二选一：
 * - RTOS：创建 Supervisor 线程
 * - 裸机：主循环周期调用 wdg_sup_poll()
 */
ret_code_t wdg_sup_start(void);
ret_code_t wdg_sup_poll(void);

/* 失败落盘 Hook（可选：你可以在别的 .c 里实现它调用 blackbox） */
void wdg_sup_fail_hook(uint8_t id, uint32_t seq, uint32_t nonce, uint32_t expected, uint32_t got,
                       uint32_t reason);

#ifdef __cplusplus
}
#endif

#endif