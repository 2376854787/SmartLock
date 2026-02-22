#ifndef HAL_WDG_H
#define HAL_WDG_H
#include <stdbool.h>
#include <stdint.h>

#include "ret_code.h"
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

#ifdef __cplusplus
}
#endif

#endif