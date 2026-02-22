#ifndef SMARTLOCK_CONFIG_CUS_H
#define SMARTLOCK_CONFIG_CUS_H

/* ============================================================================
 * L4 模块层配置
 * 负责模块私有参数（具体数值、超时、周期等）。
 * 说明：模块开关在 L2 产品层配置，不在本文件定义。
 * ============================================================================
 */

/* Watchdog App 模块参数 */
#define CFG_PARAM_WATCHDOG_APP_TIMEOUT_MS    8000u
#define CFG_PARAM_WATCHDOG_APP_SUP_PERIOD_MS 200u
#define CFG_PARAM_WATCHDOG_APP_BOOT_GRACE_MS 6000u

#endif /* SMARTLOCK_CONFIG_CUS_H */
