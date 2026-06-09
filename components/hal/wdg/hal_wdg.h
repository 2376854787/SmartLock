#ifndef HAL_WDG_H
#define HAL_WDG_H
#include <stdbool.h>
#include <stdint.h>

#include "ret_code_t.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    HAL_WDG_MODE_IWDG = 0, /* 独立看门狗 */
    HAL_WDG_MODE_WWDG = 1, /* 窗口看门狗 */
} hal_wdg_mode_t;
/* 看门狗配置 */
typedef struct {
    hal_wdg_mode_t mode;
    uint32_t timeout_ms;    /* IWDG: 超时 */
    uint32_t window_min_ms; /* WWDG: 窗口下限；IWDG 忽略 */
    bool debug_freeze;      /* 调试冻结 */
} hal_wdg_cfg_t;
ret_code_t hal_wdg_init(const hal_wdg_cfg_t *cfg);
ret_code_t hal_wdg_kick(void);
bool hal_wdg_is_inited(void);

/**
 * @brief 内部错误弱钩子函数
 * @param rc_port port 错误码
 * @param rc_hal  hal 错误码
 * @param api     api名称
 * @param arg0    参数1
 * @param arg1    参数2
 */
void hal_wdg_on_port_error(ret_code_t rc_port, ret_code_t rc_hal, const char *api, uint32_t arg0,
                           uint32_t arg1);

#ifdef __cplusplus
}
#endif

#endif
