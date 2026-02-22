#include "watchdog_app.h"
#include "APP_config.h"
#if (defined(CFG_FEAT_HAL_WDG) && (CFG_FEAT_HAL_WDG == 1)) && \
    (defined(CFG_FEAT_WDG_SUPERVISOR) && (CFG_FEAT_WDG_SUPERVISOR == 1))

#include <stdbool.h>

#include "hal_wdg.h"
#include "wdg_supervisor.h"

ret_code_t Watchdog_AppInit(void) {
    static bool s_inited = false;
    if (s_inited) return RET_OK;

    const hal_wdg_cfg_t wdg_cfg = {
        .mode          = HAL_WDG_MODE_IWDG,
        .timeout_ms    = CFG_PARAM_WATCHDOG_APP_TIMEOUT_MS,
        .window_min_ms = 0u,
        .debug_freeze  = true,
    };

    /* 初始化内部全局变量 */
    ret_code_t rc =
        wdg_sup_init(CFG_PARAM_WATCHDOG_APP_SUP_PERIOD_MS, CFG_PARAM_WATCHDOG_APP_BOOT_GRACE_MS);
    if (ret_is_err(rc)) return rc;
    /* 注册RTOS 或逻辑任务 */
    rc = wdg_sup_start();
    if (ret_is_err(rc)) return rc;
    /* 配置硬件看门狗 */
    rc = hal_wdg_init(&wdg_cfg);
    if (ret_is_err(rc)) return rc;
    /* 喂一次狗 */
    rc = hal_wdg_kick();
    if (ret_is_err(rc)) return rc;
    s_inited = true;
    return RET_OK;
}

#else
ret_code_t Watchdog_AppInit(void) {
    return RET_OK;
}
#endif
