#include "hal_pwr.h"

#include "APP_config.h"

#define PWR_HAL_PARAM(reason_) RET_MAKE_PARAM(RET_MOD_HAL, RET_SUB_HAL_PWR, (reason_))
#define PWR_HAL_STATE(reason_) RET_MAKE_STATE(RET_MOD_HAL, RET_SUB_HAL_PWR, (reason_))
#define PWR_HAL_IO(reason_)    RET_MAKE_IO(RET_MOD_HAL, RET_SUB_HAL_PWR, (reason_))

#if defined(CFG_FEAT_HAL_PWR) && (CFG_FEAT_HAL_PWR == 1)

#include <string.h>

#include "assert_cus.h"
#include "hal_pwr_port.h"
#include "osal.h"
#if defined(CFG_FEAT_LOG_SYSTEM) && (CFG_FEAT_LOG_SYSTEM == 1)
#include "log.h"
#endif

/* HAL PWR 全局运行时控制块。 */
typedef struct {
    bool initialized;                                                    /* HAL 是否已初始化 */
    hal_pwr_status_t status;                                             /* 当前 HAL 状态 */
    hal_pwr_mode_t current_mode;                                         /* 当前记录的电源模式 */
    const hal_pwr_cfg_t *cfg;                                            /* 绑定的根配置 */
    uint8_t active_config_set_id;                                        /* 当前生效配置集 id */
    hal_pwr_capability_t capability;                                     /* 平台能力缓存 */
    hal_pwr_wakeup_source_cfg_t wakeup_cfg[HAL_PWR_WAKEUP_SOURCE_COUNT]; /* 当前唤醒源配置缓存 */
} hal_pwr_ctrl_t;

static hal_pwr_ctrl_t s_pwr;

static ret_code_t pwr_map_port_to_hal(ret_code_t rc_port, const char *api, uint32_t arg0,
                                      uint32_t arg1);

__attribute__((weak)) void hal_pwr_on_port_error(ret_code_t rc_port, ret_code_t rc_hal,
                                                 const char *api, uint32_t arg0, uint32_t arg1) {
#if defined(CFG_FEAT_LOG_SYSTEM) && (CFG_FEAT_LOG_SYSTEM == 1) && \
    defined(CFG_PARAM_PWR_LOG_PORT_ERR) && (CFG_PARAM_PWR_LOG_PORT_ERR == 1)
#if defined(CFG_PARAM_PWR_LOG_PORT_ERR_IN_ISR) && (CFG_PARAM_PWR_LOG_PORT_ERR_IN_ISR == 1)
    if (ret_is_err(rc_port)) {
#else
    if (ret_is_err(rc_port) && !OSAL_in_isr()) {
#endif
        LOG_E("HAL_PWR", "api:%s port:0x%08lX->hal:0x%08lX arg0:%lu arg1:%lu",
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

static bool pwr_mode_is_valid(hal_pwr_mode_t mode) {
    return (mode == HAL_PWR_MODE_RUN) || (mode == HAL_PWR_MODE_SLEEP) ||
           (mode == HAL_PWR_MODE_STOP) || (mode == HAL_PWR_MODE_STANDBY) ||
           (mode == HAL_PWR_MODE_SHUTDOWN);
}

static bool pwr_wakeup_source_is_valid(hal_pwr_wakeup_source_t source) {
    return ((uint32_t)source < (uint32_t)HAL_PWR_WAKEUP_SOURCE_COUNT);
}

static uint32_t pwr_mode_entry_to_cap(hal_pwr_mode_entry_t entry) {
    switch (entry) {
        case HAL_PWR_MODE_ENTRY_WFI:
            return HAL_PWR_MODE_ENTRY_CAP_WFI;

        case HAL_PWR_MODE_ENTRY_WFE:
            return HAL_PWR_MODE_ENTRY_CAP_WFE;

        default:
            return HAL_PWR_MODE_ENTRY_CAP_NONE;
    }
}

static uint32_t pwr_regulator_to_cap(hal_pwr_regulator_t regulator) {
    switch (regulator) {
        case HAL_PWR_REGULATOR_MAIN:
            return HAL_PWR_REGULATOR_CAP_MAIN;

        case HAL_PWR_REGULATOR_LOW_POWER:
            return HAL_PWR_REGULATOR_CAP_LOW_POWER;

        default:
            return HAL_PWR_REGULATOR_CAP_NONE;
    }
}

static hal_pwr_wakeup_source_cfg_t pwr_build_wakeup_cfg(
    const hal_pwr_wakeup_source_t source, const hal_pwr_wakeup_source_policy_t *policy) {
    hal_pwr_wakeup_source_cfg_t cfg = {0};

    /* 设置唤醒源的 配置 */
    cfg.source                      = source;
    cfg.instance     = (policy != NULL) ? policy->instance : HAL_PWR_WAKEUP_SOURCE_INSTANCE_DEFAULT;
    cfg.enable       = (policy != NULL) ? policy->enable : false;
    cfg.option_flags = (policy != NULL) ? policy->option_flags : HAL_PWR_WAKEUP_SOURCE_OPT_NONE;
    return cfg;
}

static bool pwr_wakeup_cfg_is_default(const hal_pwr_wakeup_source_cfg_t *cfg) {
    return (cfg->instance == HAL_PWR_WAKEUP_SOURCE_INSTANCE_DEFAULT) && !cfg->enable &&
           (cfg->option_flags == HAL_PWR_WAKEUP_SOURCE_OPT_NONE);
}
/**
 * @brief 从配置集数组遍历找到指定 id 的配置集
 * @param cfg 配置集数组
 * @param config_set_id 配置id
 * @return
 */
static const hal_pwr_config_set_t *pwr_find_config_set(const hal_pwr_cfg_t *cfg,
                                                       uint8_t config_set_id) {
    uint32_t i = 0u;

    if ((cfg == NULL) || (cfg->config_sets == NULL)) return NULL;

    for (i = 0u; i < (uint32_t)cfg->config_set_count; ++i) {
        if (cfg->config_sets[i].config_set_id == config_set_id) return &cfg->config_sets[i];
    }
    return NULL;
}

static ret_code_t pwr_validate_root_cfg(const hal_pwr_cfg_t *cfg) {
    uint32_t i = 0u;
    uint32_t j = 0u;

    REQUIRE_RET(cfg != NULL, PWR_HAL_PARAM(RET_R_NULL_PTR));
    REQUIRE_RET(cfg->config_sets != NULL, PWR_HAL_PARAM(RET_R_NULL_PTR));
    REQUIRE_RET(cfg->config_set_count > 0u, PWR_HAL_PARAM(RET_R_RANGE_ERR));
    REQUIRE_RET(pwr_find_config_set(cfg, cfg->default_config_set_id) != NULL,
                PWR_HAL_PARAM(RET_R_INVALID_ARG));

    /* 判断 id 唯一 */
    for (i = 0u; i < (uint32_t)cfg->config_set_count; ++i) {
        for (j = i + 1u; j < (uint32_t)cfg->config_set_count; ++j) {
            REQUIRE_RET(cfg->config_sets[i].config_set_id != cfg->config_sets[j].config_set_id,
                        PWR_HAL_PARAM(RET_R_INVALID_ARG));
        }
    }
    return RET_OK;
}

static ret_code_t pwr_validate_mode_cfg_against_capability(hal_pwr_mode_t mode,
                                                           const hal_pwr_mode_cfg_t *mode_cfg) {
    REQUIRE_RET(mode_cfg != NULL, PWR_HAL_PARAM(RET_R_NULL_PTR));

    /* 映射模式到对应的位图 */
    const uint32_t entry_cap     = pwr_mode_entry_to_cap(mode_cfg->entry);
    const uint32_t regulator_cap = pwr_regulator_to_cap(mode_cfg->regulator);

    /* 必须能力支持 */
    if ((s_pwr.capability.supported_modes_mask & HAL_PWR_MODE_MASK(mode)) == 0u) {
        return PWR_HAL_PARAM(RET_R_UNSUPPORTED);
    }
    /* 对应的模式必须平台支持 */
    if ((mode == HAL_PWR_MODE_SLEEP) || (mode == HAL_PWR_MODE_STOP)) {
        REQUIRE_RET(entry_cap != HAL_PWR_MODE_ENTRY_CAP_NONE, PWR_HAL_PARAM(RET_R_INVALID_ARG));
        REQUIRE_RET((s_pwr.capability.mode_entry_cap[mode] & entry_cap) != 0u,
                    PWR_HAL_PARAM(RET_R_UNSUPPORTED));
    }
    /*  能力必须支持 */
    if ((mode == HAL_PWR_MODE_STOP) && (s_pwr.capability.regulator_cap[mode] != 0u)) {
        REQUIRE_RET(regulator_cap != HAL_PWR_REGULATOR_CAP_NONE, PWR_HAL_PARAM(RET_R_INVALID_ARG));
        REQUIRE_RET((s_pwr.capability.regulator_cap[mode] & regulator_cap) != 0u,
                    PWR_HAL_PARAM(RET_R_UNSUPPORTED));
    }
    return RET_OK;
}
/**
 * @brief 将指定的唤醒源配置 在底层进行实现
 * @param cfg 唤醒源配置
 * @return
 */
static ret_code_t pwr_apply_wakeup_cfg(const hal_pwr_wakeup_source_cfg_t *cfg) {
    /* 有可配置的唤醒图 且传入的位图是支持的*/
    const bool configurable = ((s_pwr.capability.configurable_wakeup_source_mask &
                                HAL_PWR_WAKEUP_SOURCE_MASK(cfg->source)) != 0u);

    if (!configurable) {
        REQUIRE_RET(pwr_wakeup_cfg_is_default(cfg), PWR_HAL_PARAM(RET_R_UNSUPPORTED));
        s_pwr.wakeup_cfg[cfg->source] = *cfg;
        return RET_OK;
    }

    /* 底层开始对应的唤醒源 */
    {
        const ret_code_t rc = hal_pwr_port_configure_wakeup_source(cfg);
        if (ret_is_err(rc))
            return pwr_map_port_to_hal(rc, "hal_pwr_port_configure_wakeup_source",
                                       (uint32_t)cfg->source, (uint32_t)cfg->instance);
    }
    s_pwr.wakeup_cfg[cfg->source] = *cfg;
    return RET_OK;
}
/**
 * @brief 对单个配置集的配置应用到平台
 * @param config_set 单个配置集
 * @return
 */
static ret_code_t pwr_apply_config_set_internal(const hal_pwr_config_set_t *config_set) {
    uint32_t i    = 0u;
    ret_code_t rc = RET_OK;

    REQUIRE_RET(config_set != NULL, PWR_HAL_PARAM(RET_R_NULL_PTR));

    /* 应用配置集到平台 备份域、是否清理标志位*/
    rc = hal_pwr_port_apply_config_set(config_set);
    if (ret_is_err(rc))
        return pwr_map_port_to_hal(rc, "hal_pwr_port_apply_config_set",
                                   (uint32_t)config_set->config_set_id, 0u);

    /* 遍历找到一个支持的功能位图*/
    for (i = 0u; i < (uint32_t)HAL_PWR_MODE_COUNT; ++i) {
        if ((hal_pwr_mode_t)i == HAL_PWR_MODE_RUN) continue;
        /* 去除掉不支持的模式 */
        if ((s_pwr.capability.supported_modes_mask & HAL_PWR_MODE_MASK((hal_pwr_mode_t)i)) == 0u)
            continue;
        /* 库支持的模式中 检查平台是否支持该模式 */
        rc = pwr_validate_mode_cfg_against_capability((hal_pwr_mode_t)i, &config_set->mode_cfg[i]);
        if (ret_is_err(rc)) return rc;
    }

    /* 遍历唤醒源 填充为一个策略表返回 判断根据策略表开启底层的唤醒源 */
    for (i = 0u; i < (uint32_t)HAL_PWR_WAKEUP_SOURCE_COUNT; ++i) {
        const hal_pwr_wakeup_source_cfg_t wakeup_cfg =
            pwr_build_wakeup_cfg((hal_pwr_wakeup_source_t)i, &config_set->wakeup_source_cfg[i]);
        rc = pwr_apply_wakeup_cfg(&wakeup_cfg);
        if (ret_is_err(rc)) return rc;
    }

    /* 更新当前使用的配置集 */
    s_pwr.active_config_set_id = config_set->config_set_id;
    return RET_OK;
}

static ret_code_t pwr_map_port_to_hal(ret_code_t rc_port, const char *api, uint32_t arg0,
                                      uint32_t arg1) {
    ret_code_t rc_hal = PWR_HAL_IO(RET_R_IO);

    if (ret_is_ok(rc_port)) return RET_OK;
    if (ret_is_class(rc_port, RET_CLASS_PARAM)) {
        if (ret_is_reason(rc_port, RET_R_NULL_PTR))
            rc_hal = PWR_HAL_PARAM(RET_R_NULL_PTR);
        else if (ret_is_reason(rc_port, RET_R_RANGE_ERR))
            rc_hal = PWR_HAL_PARAM(RET_R_RANGE_ERR);
        else if (ret_is_reason(rc_port, RET_R_UNSUPPORTED))
            rc_hal = PWR_HAL_PARAM(RET_R_UNSUPPORTED);
        else
            rc_hal = PWR_HAL_PARAM(RET_R_INVALID_ARG);
    } else if (ret_is_class(rc_port, RET_CLASS_STATE)) {
        if (ret_is_reason(rc_port, RET_R_BUSY))
            rc_hal = PWR_HAL_STATE(RET_R_BUSY);
        else if (ret_is_reason(rc_port, RET_R_NOT_READY))
            rc_hal = PWR_HAL_STATE(RET_R_NOT_READY);
        else
            rc_hal = PWR_HAL_STATE(RET_R_STATE_ERR);
    }

    hal_pwr_on_port_error(rc_port, rc_hal, api, arg0, arg1);
    return rc_hal;
}

static hal_pwr_reset_reason_t pwr_reset_raw_to_reason(uint32_t raw_value) {
    if ((raw_value & HAL_PWR_RESET_RAW_BROWN_OUT) != 0u) return HAL_PWR_RESET_REASON_BROWN_OUT;
    if ((raw_value & (HAL_PWR_RESET_RAW_IWDG | HAL_PWR_RESET_RAW_WWDG)) != 0u)
        return HAL_PWR_RESET_REASON_WATCHDOG;
    if ((raw_value & HAL_PWR_RESET_RAW_SOFTWARE) != 0u) return HAL_PWR_RESET_REASON_SOFTWARE;
    if ((raw_value & HAL_PWR_RESET_RAW_STANDBY) != 0u) return HAL_PWR_RESET_REASON_STANDBY_WAKEUP;
    if ((raw_value & HAL_PWR_RESET_RAW_PIN) != 0u) return HAL_PWR_RESET_REASON_PIN;
    if ((raw_value & HAL_PWR_RESET_RAW_POWER_ON) != 0u) return HAL_PWR_RESET_REASON_POWER_ON;
    return HAL_PWR_RESET_REASON_UNDEFINED;
}
/**
 * @brief 根据配置进行初始化
 * @param cfg 根配置
 * @return 状态码
 */
ret_code_t hal_pwr_init(const hal_pwr_cfg_t *cfg) {
    ret_code_t rc = RET_OK;

    /* 检查参数 */
    rc            = pwr_validate_root_cfg(cfg);
    if (ret_is_err(rc)) return rc;
    REQUIRE_RET(!s_pwr.initialized, PWR_HAL_STATE(RET_R_BUSY));

    /* 调用底层初始化pwr */
    rc = hal_pwr_port_init();
    if (ret_is_err(rc)) return pwr_map_port_to_hal(rc, "hal_pwr_port_init", 0u, 0u);

    /* 获取平台能力 */
    rc = hal_pwr_port_get_capability(&s_pwr.capability);
    if (ret_is_err(rc)) {
        (void)hal_pwr_port_deinit();
        return pwr_map_port_to_hal(rc, "hal_pwr_port_get_capability", 0u, 0u);
    }

    /* 更新内部状态 */
    s_pwr.cfg          = cfg;
    s_pwr.current_mode = HAL_PWR_MODE_RUN;
    s_pwr.status       = HAL_PWR_STATUS_READY;
    s_pwr.initialized  = true;

    rc                 = hal_pwr_select_config_set(cfg->default_config_set_id);
    if (ret_is_err(rc)) {
        (void)hal_pwr_port_deinit();
        memset(&s_pwr, 0, sizeof(s_pwr));
        return rc;
    }
    return RET_OK;
}

ret_code_t hal_pwr_deinit(void) {
    REQUIRE_RET(s_pwr.initialized, PWR_HAL_STATE(RET_R_NOT_READY));

    {
        const ret_code_t rc = hal_pwr_port_deinit();
        if (ret_is_err(rc)) return pwr_map_port_to_hal(rc, "hal_pwr_port_deinit", 0u, 0u);
    }

    memset(&s_pwr, 0, sizeof(s_pwr));
    return RET_OK;
}

ret_code_t hal_pwr_init_check(const hal_pwr_cfg_t *cfg, bool *out_is_match) {
    REQUIRE_RET(cfg != NULL, PWR_HAL_PARAM(RET_R_NULL_PTR));
    REQUIRE_RET(out_is_match != NULL, PWR_HAL_PARAM(RET_R_NULL_PTR));

    *out_is_match = s_pwr.initialized && (s_pwr.cfg == cfg);
    return RET_OK;
}

ret_code_t hal_pwr_get_status(hal_pwr_status_t *out) {
    REQUIRE_RET(out != NULL, PWR_HAL_PARAM(RET_R_NULL_PTR));
    *out = s_pwr.initialized ? s_pwr.status : HAL_PWR_STATUS_UNINIT;
    return RET_OK;
}

ret_code_t hal_pwr_get_capability(hal_pwr_capability_t *out) {
    REQUIRE_RET(out != NULL, PWR_HAL_PARAM(RET_R_NULL_PTR));
    REQUIRE_RET(s_pwr.initialized, PWR_HAL_STATE(RET_R_NOT_READY));

    *out = s_pwr.capability;
    return RET_OK;
}
/**
 * @brief 找到id 对应的配置集然后应用配置
 * @param config_set_id id
 * @return 状态码
 */
ret_code_t hal_pwr_select_config_set(uint8_t config_set_id) {
    const hal_pwr_config_set_t *config_set = NULL;

    REQUIRE_RET(s_pwr.initialized, PWR_HAL_STATE(RET_R_NOT_READY));

    /* 找到指定的配置集 */
    config_set = pwr_find_config_set(s_pwr.cfg, config_set_id);
    REQUIRE_RET(config_set != NULL, PWR_HAL_PARAM(RET_R_INVALID_ARG));
    /* 应用配置集 */
    return pwr_apply_config_set_internal(config_set);
}

ret_code_t hal_pwr_get_active_config_set(uint8_t *out_config_set_id) {
    REQUIRE_RET(out_config_set_id != NULL, PWR_HAL_PARAM(RET_R_NULL_PTR));
    REQUIRE_RET(s_pwr.initialized, PWR_HAL_STATE(RET_R_NOT_READY));

    *out_config_set_id = s_pwr.active_config_set_id;
    return RET_OK;
}

ret_code_t hal_pwr_get_mode(hal_pwr_mode_t *out_mode) {
    REQUIRE_RET(out_mode != NULL, PWR_HAL_PARAM(RET_R_NULL_PTR));
    REQUIRE_RET(s_pwr.initialized, PWR_HAL_STATE(RET_R_NOT_READY));

    *out_mode = s_pwr.current_mode;
    return RET_OK;
}

ret_code_t hal_pwr_set_mode(hal_pwr_mode_t mode) {
    const hal_pwr_config_set_t *config_set = NULL;
    ret_code_t rc                          = RET_OK;

    REQUIRE_RET(s_pwr.initialized, PWR_HAL_STATE(RET_R_NOT_READY));
    REQUIRE_RET(pwr_mode_is_valid(mode), PWR_HAL_PARAM(RET_R_INVALID_ARG));
    REQUIRE_RET((s_pwr.capability.supported_modes_mask & HAL_PWR_MODE_MASK(mode)) != 0u,
                PWR_HAL_PARAM(RET_R_UNSUPPORTED));

    if (mode == HAL_PWR_MODE_RUN) {
        s_pwr.current_mode = HAL_PWR_MODE_RUN;
        return RET_OK;
    }

    config_set = pwr_find_config_set(s_pwr.cfg, s_pwr.active_config_set_id);
    REQUIRE_RET(config_set != NULL, PWR_HAL_STATE(RET_R_STATE_ERR));

    rc = pwr_validate_mode_cfg_against_capability(mode, &config_set->mode_cfg[mode]);
    if (ret_is_err(rc)) return rc;

    s_pwr.status       = HAL_PWR_STATUS_BUSY;
    s_pwr.current_mode = mode;
    rc                 = hal_pwr_port_set_mode(mode, &config_set->mode_cfg[mode]);
    s_pwr.status       = HAL_PWR_STATUS_READY;
    if ((mode == HAL_PWR_MODE_SLEEP) || (mode == HAL_PWR_MODE_STOP) || ret_is_err(rc))
        s_pwr.current_mode = HAL_PWR_MODE_RUN;
    if (ret_is_err(rc)) return pwr_map_port_to_hal(rc, "hal_pwr_port_set_mode", (uint32_t)mode, 0u);
    return RET_OK;
}
/**
 * @brief 根据唤醒源配置进行应用
 * @param cfg 传入指定的唤醒源配置
 * @return
 */
ret_code_t hal_pwr_configure_wakeup_source(const hal_pwr_wakeup_source_cfg_t *cfg) {
    REQUIRE_RET(cfg != NULL, PWR_HAL_PARAM(RET_R_NULL_PTR));
    REQUIRE_RET(s_pwr.initialized, PWR_HAL_STATE(RET_R_NOT_READY));
    REQUIRE_RET(pwr_wakeup_source_is_valid(cfg->source), PWR_HAL_PARAM(RET_R_INVALID_ARG));
    return pwr_apply_wakeup_cfg(cfg);
}

ret_code_t hal_pwr_enable_wakeup_source(hal_pwr_wakeup_source_t source) {
    hal_pwr_wakeup_source_cfg_t cfg = {0};

    REQUIRE_RET(s_pwr.initialized, PWR_HAL_STATE(RET_R_NOT_READY));
    REQUIRE_RET(pwr_wakeup_source_is_valid(source), PWR_HAL_PARAM(RET_R_INVALID_ARG));

    cfg        = s_pwr.wakeup_cfg[source];
    cfg.enable = true;
    return pwr_apply_wakeup_cfg(&cfg);
}

ret_code_t hal_pwr_disable_wakeup_source(hal_pwr_wakeup_source_t source) {
    hal_pwr_wakeup_source_cfg_t cfg = {0};

    REQUIRE_RET(s_pwr.initialized, PWR_HAL_STATE(RET_R_NOT_READY));
    REQUIRE_RET(pwr_wakeup_source_is_valid(source), PWR_HAL_PARAM(RET_R_INVALID_ARG));

    cfg        = s_pwr.wakeup_cfg[source];
    cfg.enable = false;
    return pwr_apply_wakeup_cfg(&cfg);
}

ret_code_t hal_pwr_get_wakeup_source_cfg(hal_pwr_wakeup_source_t source,
                                         hal_pwr_wakeup_source_cfg_t *out_cfg) {
    REQUIRE_RET(out_cfg != NULL, PWR_HAL_PARAM(RET_R_NULL_PTR));
    REQUIRE_RET(s_pwr.initialized, PWR_HAL_STATE(RET_R_NOT_READY));
    REQUIRE_RET(pwr_wakeup_source_is_valid(source), PWR_HAL_PARAM(RET_R_INVALID_ARG));

    *out_cfg = s_pwr.wakeup_cfg[source];
    return RET_OK;
}

ret_code_t hal_pwr_get_reset_reason(hal_pwr_reset_reason_t *out_reason) {
    uint32_t raw_value = HAL_PWR_RESET_RAW_NONE;
    ret_code_t rc      = RET_OK;

    REQUIRE_RET(out_reason != NULL, PWR_HAL_PARAM(RET_R_NULL_PTR));
    REQUIRE_RET(s_pwr.initialized, PWR_HAL_STATE(RET_R_NOT_READY));

    rc = hal_pwr_port_get_reset_raw_value(&raw_value);
    if (ret_is_err(rc)) return pwr_map_port_to_hal(rc, "hal_pwr_port_get_reset_raw_value", 0u, 0u);

    *out_reason = pwr_reset_raw_to_reason(raw_value);
    return RET_OK;
}

ret_code_t hal_pwr_get_reset_raw_value(uint32_t *out_raw_value) {
    REQUIRE_RET(out_raw_value != NULL, PWR_HAL_PARAM(RET_R_NULL_PTR));
    REQUIRE_RET(s_pwr.initialized, PWR_HAL_STATE(RET_R_NOT_READY));

    {
        const ret_code_t rc = hal_pwr_port_get_reset_raw_value(out_raw_value);
        if (ret_is_err(rc))
            return pwr_map_port_to_hal(rc, "hal_pwr_port_get_reset_raw_value", 0u, 0u);
    }
    return RET_OK;
}

ret_code_t hal_pwr_clear_reset_flags(void) {
    REQUIRE_RET(s_pwr.initialized, PWR_HAL_STATE(RET_R_NOT_READY));

    {
        const ret_code_t rc = hal_pwr_port_clear_reset_flags();
        if (ret_is_err(rc))
            return pwr_map_port_to_hal(rc, "hal_pwr_port_clear_reset_flags", 0u, 0u);
    }
    return RET_OK;
}

ret_code_t hal_pwr_get_wakeup_reason(uint32_t *out_mask) {
    REQUIRE_RET(out_mask != NULL, PWR_HAL_PARAM(RET_R_NULL_PTR));
    REQUIRE_RET(s_pwr.initialized, PWR_HAL_STATE(RET_R_NOT_READY));

    {
        const ret_code_t rc = hal_pwr_port_get_wakeup_reason(out_mask);
        if (ret_is_err(rc))
            return pwr_map_port_to_hal(rc, "hal_pwr_port_get_wakeup_reason", 0u, 0u);
    }
    return RET_OK;
}

ret_code_t hal_pwr_clear_wakeup_reason(uint32_t mask) {
    REQUIRE_RET(s_pwr.initialized, PWR_HAL_STATE(RET_R_NOT_READY));

    {
        const ret_code_t rc = hal_pwr_port_clear_wakeup_reason(mask);
        if (ret_is_err(rc))
            return pwr_map_port_to_hal(rc, "hal_pwr_port_clear_wakeup_reason", mask, 0u);
    }
    return RET_OK;
}

#else

void hal_pwr_on_port_error(ret_code_t rc_port, ret_code_t rc_hal, const char *api, uint32_t arg0,
                           uint32_t arg1) {
    (void)rc_port;
    (void)rc_hal;
    (void)api;
    (void)arg0;
    (void)arg1;
}

ret_code_t hal_pwr_init(const hal_pwr_cfg_t *cfg) {
    (void)cfg;
    return PWR_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_pwr_deinit(void) {
    return PWR_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_pwr_init_check(const hal_pwr_cfg_t *cfg, bool *out_is_match) {
    (void)cfg;
    if (out_is_match != NULL) *out_is_match = false;
    return PWR_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_pwr_get_status(hal_pwr_status_t *out) {
    if (out != NULL) *out = HAL_PWR_STATUS_UNINIT;
    return PWR_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_pwr_get_capability(hal_pwr_capability_t *out) {
    (void)out;
    return PWR_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_pwr_select_config_set(uint8_t config_set_id) {
    (void)config_set_id;
    return PWR_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_pwr_get_active_config_set(uint8_t *out_config_set_id) {
    (void)out_config_set_id;
    return PWR_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_pwr_get_mode(hal_pwr_mode_t *out_mode) {
    if (out_mode != NULL) *out_mode = HAL_PWR_MODE_RUN;
    return PWR_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_pwr_set_mode(hal_pwr_mode_t mode) {
    (void)mode;
    return PWR_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_pwr_configure_wakeup_source(const hal_pwr_wakeup_source_cfg_t *cfg) {
    (void)cfg;
    return PWR_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_pwr_enable_wakeup_source(hal_pwr_wakeup_source_t source) {
    (void)source;
    return PWR_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_pwr_disable_wakeup_source(hal_pwr_wakeup_source_t source) {
    (void)source;
    return PWR_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_pwr_get_wakeup_source_cfg(hal_pwr_wakeup_source_t source,
                                         hal_pwr_wakeup_source_cfg_t *out_cfg) {
    (void)source;
    (void)out_cfg;
    return PWR_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_pwr_get_reset_reason(hal_pwr_reset_reason_t *out_reason) {
    if (out_reason != NULL) *out_reason = HAL_PWR_RESET_REASON_UNDEFINED;
    return PWR_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_pwr_get_reset_raw_value(uint32_t *out_raw_value) {
    (void)out_raw_value;
    return PWR_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_pwr_clear_reset_flags(void) {
    return PWR_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_pwr_get_wakeup_reason(uint32_t *out_mask) {
    (void)out_mask;
    return PWR_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_pwr_clear_wakeup_reason(uint32_t mask) {
    (void)mask;
    return PWR_HAL_PARAM(RET_R_UNSUPPORTED);
}

#endif
