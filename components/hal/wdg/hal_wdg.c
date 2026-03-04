#include "hal_wdg.h"

#include "APP_config.h"

#define WDG_HAL_PARAM(reason_)   RET_MAKE_PARAM(RET_MOD_HAL, RET_SUB_HAL_WDT, (reason_))
#define WDG_HAL_STATE(reason_)   RET_MAKE_STATE(RET_MOD_HAL, RET_SUB_HAL_WDT, (reason_))
#define WDG_HAL_TIMEOUT(reason_) RET_MAKE_TIMEOUT(RET_MOD_HAL, RET_SUB_HAL_WDT, (reason_))
#define WDG_HAL_IO(reason_)      RET_MAKE_IO(RET_MOD_HAL, RET_SUB_HAL_WDT, (reason_))
#define WDG_HAL_RES(reason_)     RET_MAKE_RESOURCE(RET_MOD_HAL, RET_SUB_HAL_WDT, (reason_))
#define WDG_HAL_FATAL(reason_)   RET_MAKE_FATAL(RET_MOD_HAL, RET_SUB_HAL_WDT, (reason_))

#if (defined(CFG_FEAT_HAL_WDG) && (CFG_FEAT_HAL_WDG == 1))
#include <stddef.h>

#include "assert_cus.h"
#include "hal_wdg_port.h"
#include "osal.h"
#if defined(CFG_FEAT_LOG_SYSTEM) && (CFG_FEAT_LOG_SYSTEM == 1)
#include "log.h"
#endif

static volatile bool s_inited = false;

/**
 * @brief port 层错误上报钩子（默认空实现，可在外部重写）
 * @param rc_port port 层错误码
 * @param rc_hal  映射后的 HAL 错误码
 * @param api     触发 API 名称
 * @param arg0    辅助参数0
 * @param arg1    辅助参数1
 */
__attribute__((weak)) void hal_wdg_on_port_error(ret_code_t rc_port, ret_code_t rc_hal,
                                                 const char* api, uint32_t arg0, uint32_t arg1) {
#if defined(CFG_FEAT_LOG_SYSTEM) && (CFG_FEAT_LOG_SYSTEM == 1) && \
    defined(CFG_PARAM_WDG_LOG_PORT_ERR) && (CFG_PARAM_WDG_LOG_PORT_ERR == 1)
#if defined(CFG_PARAM_WDG_LOG_PORT_ERR_IN_ISR) && (CFG_PARAM_WDG_LOG_PORT_ERR_IN_ISR == 1)
    if (ret_is_err(rc_port)) {
#else
    if (ret_is_err(rc_port) && !OSAL_in_isr()) {
#endif
        LOG_E("HAL_WDG", "api:%s port:0x%08lX->hal:0x%08lX arg0:%lu arg1:%lu",
              (api != NULL) ? api : "unknown", (unsigned long)rc_port, (unsigned long)rc_hal,
              (unsigned long)arg0, (unsigned long)arg1);
    }
#else
    (void)rc_port;
    (void)rc_hal;
    (void)api;
    (void)arg0;
    (void)arg1;
#endif
}

static inline ret_code_t wdg_map_port_to_hal(ret_code_t rc_port, const char* api, uint32_t arg0,
                                             uint32_t arg1) {
    if (ret_is_ok(rc_port)) return RET_OK;

    ret_code_t rc_hal = WDG_HAL_IO(RET_R_IO);

    if (ret_is_class(rc_port, RET_CLASS_PARAM)) {
        if (ret_is_reason(rc_port, RET_R_NULL_PTR))
            rc_hal = WDG_HAL_PARAM(RET_R_NULL_PTR);
        else if (ret_is_reason(rc_port, RET_R_RANGE_ERR))
            rc_hal = WDG_HAL_PARAM(RET_R_RANGE_ERR);
        else if (ret_is_reason(rc_port, RET_R_UNSUPPORTED))
            rc_hal = WDG_HAL_PARAM(RET_R_UNSUPPORTED);
        else
            rc_hal = WDG_HAL_PARAM(RET_R_INVALID_ARG);
    } else if (ret_is_class(rc_port, RET_CLASS_TIMEOUT)) {
        rc_hal = WDG_HAL_TIMEOUT(RET_R_TIMEOUT);
    } else if (ret_is_class(rc_port, RET_CLASS_RESOURCE)) {
        if (ret_is_reason(rc_port, RET_R_NO_MEM))
            rc_hal = WDG_HAL_RES(RET_R_NO_MEM);
        else
            rc_hal = WDG_HAL_RES(RET_R_NO_RESOURCE);
    } else if (ret_is_class(rc_port, RET_CLASS_STATE)) {
        if (ret_is_reason(rc_port, RET_R_BUSY))
            rc_hal = WDG_HAL_STATE(RET_R_BUSY);
        else if (ret_is_reason(rc_port, RET_R_NOT_READY))
            rc_hal = WDG_HAL_STATE(RET_R_NOT_READY);
        else
            rc_hal = WDG_HAL_STATE(RET_R_STATE_ERR);
    } else if (ret_is_class(rc_port, RET_CLASS_FATAL)) {
        rc_hal = WDG_HAL_FATAL(RET_R_PANIC);
    }

    hal_wdg_on_port_error(rc_port, rc_hal, api, arg0, arg1);
    return rc_hal;
}

/**
 * @brief 检查配置是否是合法的
 * @param cfg 配置
 * @return
 */
static ret_code_t wdg_cfg_check(const hal_wdg_cfg_t* cfg) {
    ASSERT_PARAM(cfg != NULL);
    REQUIRE_RET(cfg != NULL, WDG_HAL_PARAM(RET_R_NULL_PTR));
    REQUIRE_RET((cfg->mode == HAL_WDG_MODE_IWDG) || (cfg->mode == HAL_WDG_MODE_WWDG),
                WDG_HAL_PARAM(RET_R_INVALID_ARG));
    REQUIRE_RET(cfg->timeout_ms != 0u, WDG_HAL_PARAM(RET_R_RANGE_ERR));

    /*　窗口看门狗　且　超时时间大于窗口　或　等于０　*/
    if (cfg->mode == HAL_WDG_MODE_WWDG) {
        REQUIRE_RET((cfg->window_min_ms != 0u) && (cfg->window_min_ms < cfg->timeout_ms),
                    WDG_HAL_PARAM(RET_R_RANGE_ERR));
    }
    return RET_OK;
}

bool hal_wdg_is_inited(void) {
    return s_inited;
}

/**
 * @brief 初始化硬件看门狗配置
 * @param cfg 配置
 * @return
 */
ret_code_t hal_wdg_init(const hal_wdg_cfg_t* cfg) {
    /* 检查参数 */
    ret_code_t rc = wdg_cfg_check(cfg);
    if (ret_is_err(rc)) return rc;
    REQUIRE_RET(!s_inited, WDG_HAL_STATE(RET_R_BUSY));

    /* 初始化 看门狗 */
    rc = hal_wdg_port_init(cfg);
    if (ret_is_err(rc))
        return wdg_map_port_to_hal(rc, "hal_wdg_port_init", cfg->timeout_ms, cfg->window_min_ms);

    s_inited = true;
    return RET_OK;
}

/**
 * @brief 喂狗
 * @return 32尾状态码
 */
ret_code_t hal_wdg_kick(void) {
    if (!s_inited) return WDG_HAL_STATE(RET_R_NOT_READY);

    const ret_code_t rc = hal_wdg_port_kick();
    if (ret_is_err(rc)) return wdg_map_port_to_hal(rc, "hal_wdg_port_kick", 0u, 0u);
    return RET_OK;
}

#else

void hal_wdg_on_port_error(ret_code_t rc_port, ret_code_t rc_hal, const char* api, uint32_t arg0,
                           uint32_t arg1) {
    (void)rc_port;
    (void)rc_hal;
    (void)api;
    (void)arg0;
    (void)arg1;
}

bool hal_wdg_is_inited(void) {
    return false;
}

ret_code_t hal_wdg_init(const hal_wdg_cfg_t* cfg) {
    (void)cfg;
    return WDG_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_wdg_kick(void) {
    return WDG_HAL_PARAM(RET_R_UNSUPPORTED);
}

#endif
