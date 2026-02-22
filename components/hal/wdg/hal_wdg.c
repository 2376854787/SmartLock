#include "APP_config.h"

#if (defined(CFG_FEAT_HAL_WDG) && (CFG_FEAT_HAL_WDG == 1))
#include <stddef.h>

#include "hal_wdg.h"
#include "hal_wdg_port.h"

static volatile bool s_inited = false;
/**
 * @brief 检查配置是否是合法的
 * @param cfg 配置
 * @return
 */
static ret_code_t wdg_cfg_check(const hal_wdg_cfg_t *cfg) {
    if (cfg == NULL) return RET_MAKE_PARAM(RET_MOD_HAL, RET_SUB_HAL_WDT, RET_R_NULL_PTR);
    if (cfg->timeout_ms == 0u) return RET_MAKE_PARAM(RET_MOD_HAL, RET_SUB_HAL_WDT, RET_R_RANGE_ERR);

    /*　窗口看门狗　且　超时时间大于窗口　或　等于０　*/
    if (cfg->mode == HAL_WDG_MODE_WWDG) {
        if (cfg->window_min_ms == 0u || cfg->window_min_ms >= cfg->timeout_ms) {
            return RET_MAKE_PARAM(RET_MOD_HAL, RET_SUB_HAL_WDT, RET_R_RANGE_ERR);
        }
    }
    return RET_OK;
}

bool hal_wdg_is_inited(void) {
    return s_inited;
}
/**
 * @brief 初始化
 * @param cfg 配置
 * @return
 */
ret_code_t hal_wdg_init(const hal_wdg_cfg_t *cfg) {
    /* 检查参数 */
    ret_code_t rc = wdg_cfg_check(cfg);
    if (ret_is_err(rc)) return rc;
    /* 初始化 看门狗 */
    rc = hal_wdg_port_init(cfg);
    if (ret_is_ok(rc)) s_inited = true;
    return rc;
}
/**
 * @brief 喂狗
 * @return 32尾状态码
 */
ret_code_t hal_wdg_kick(void) {
    if (!s_inited) return RET_MAKE_STATE(RET_MOD_HAL, RET_SUB_HAL_WDT, RET_R_NOT_READY);
    return hal_wdg_port_kick();
}

#endif
