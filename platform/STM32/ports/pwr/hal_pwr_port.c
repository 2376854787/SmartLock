#include "APP_config.h"

#if defined(CFG_TARGET_PLATFORM_STM32_HAL) && defined(CFG_FEAT_HAL_PWR) && (CFG_FEAT_HAL_PWR == 1)

#include "assert_cus.h"
#include "hal_pwr_port.h"
#include "stm32_hal.h"

#define PWR_PORT_PARAM(reason_) RET_MAKE_PARAM(RET_MOD_PORT, RET_SUB_PORT_PWR, (reason_))

static uint32_t pwr_port_map_entry(hal_pwr_mode_entry_t entry) {
    switch (entry) {
        case HAL_PWR_MODE_ENTRY_WFI:
            return PWR_SLEEPENTRY_WFI;

        case HAL_PWR_MODE_ENTRY_WFE:
            return PWR_SLEEPENTRY_WFE;

        default:
            return UINT32_MAX;
    }
}

static uint32_t pwr_port_map_stop_regulator(hal_pwr_regulator_t regulator) {
    switch (regulator) {
        case HAL_PWR_REGULATOR_MAIN:
            return PWR_MAINREGULATOR_ON;

        case HAL_PWR_REGULATOR_LOW_POWER:
            return PWR_LOWPOWERREGULATOR_ON;

        default:
            return UINT32_MAX;
    }
}

static void pwr_clear_rtc_flags(uint32_t mask) {
    if ((RCC->BDCR & RCC_BDCR_RTCEN) == 0u) return;

    RTC->WPR = 0xCAu;
    RTC->WPR = 0x53u;
    CLEAR_BIT(RTC->ISR, mask);
    RTC->WPR = 0xFFu;
}
/* 使能时钟 */
ret_code_t hal_pwr_port_init(void) {
    __HAL_RCC_PWR_CLK_ENABLE();
    return RET_OK;
}

ret_code_t hal_pwr_port_deinit(void) {
    __HAL_RCC_PWR_CLK_DISABLE();
    return RET_OK;
}

ret_code_t hal_pwr_port_apply_config_set(const hal_pwr_config_set_t *cfg_set) {
    REQUIRE_RET(cfg_set != NULL, PWR_PORT_PARAM(RET_R_NULL_PTR));

    if (cfg_set->allow_backup_access) HAL_PWR_EnableBkUpAccess();
    if (cfg_set->clear_wakeup_flags_on_init) __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
    if (cfg_set->clear_standby_flag_on_init) __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SB);
    return RET_OK;
}
/**
 * @brief 获取当前平台的能力
 * @param out 输出当前平台的能力
 * @return 状态码
 */
ret_code_t hal_pwr_port_get_capability(hal_pwr_capability_t *out) {
    REQUIRE_RET(out != NULL, PWR_PORT_PARAM(RET_R_NULL_PTR));

    *out = (hal_pwr_capability_t){0};
    /* 四种低功耗模式 */
    out->supported_modes_mask =
        HAL_PWR_MODE_MASK(HAL_PWR_MODE_RUN) | HAL_PWR_MODE_MASK(HAL_PWR_MODE_SLEEP) |
        HAL_PWR_MODE_MASK(HAL_PWR_MODE_STOP) | HAL_PWR_MODE_MASK(HAL_PWR_MODE_STANDBY);
    /* 一种唤醒源 */
    out->configurable_wakeup_source_mask = HAL_PWR_WAKEUP_SOURCE_MASK(HAL_PWR_WAKEUP_SOURCE_PIN);
    /* 四种唤醒原因 */
    out->observable_wakeup_reason_mask =
        HAL_PWR_WAKEUP_REASON_PIN | HAL_PWR_WAKEUP_REASON_RTC_ALARM |
        HAL_PWR_WAKEUP_REASON_RTC_WAKEUP | HAL_PWR_WAKEUP_REASON_RESET;
    /* sleep 的进入方式 */
    out->mode_entry_cap[HAL_PWR_MODE_SLEEP] =
        HAL_PWR_MODE_ENTRY_CAP_WFI | HAL_PWR_MODE_ENTRY_CAP_WFE;
    /* stop 的进入方式 */
    out->mode_entry_cap[HAL_PWR_MODE_STOP] =
        HAL_PWR_MODE_ENTRY_CAP_WFI | HAL_PWR_MODE_ENTRY_CAP_WFE;
    /* sleep 的稳压能力*/
    out->regulator_cap[HAL_PWR_MODE_SLEEP] = HAL_PWR_REGULATOR_CAP_MAIN;
    /* stop 的稳压能力 */
    out->regulator_cap[HAL_PWR_MODE_STOP] =
        HAL_PWR_REGULATOR_CAP_MAIN | HAL_PWR_REGULATOR_CAP_LOW_POWER;
    return RET_OK;
}

ret_code_t hal_pwr_port_get_reset_raw_value(uint32_t *out_raw_value) {
    REQUIRE_RET(out_raw_value != NULL, PWR_PORT_PARAM(RET_R_NULL_PTR));

    *out_raw_value = HAL_PWR_RESET_RAW_NONE;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST)) *out_raw_value |= HAL_PWR_RESET_RAW_POWER_ON;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST)) *out_raw_value |= HAL_PWR_RESET_RAW_PIN;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST)) *out_raw_value |= HAL_PWR_RESET_RAW_SOFTWARE;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST)) *out_raw_value |= HAL_PWR_RESET_RAW_IWDG;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST)) *out_raw_value |= HAL_PWR_RESET_RAW_WWDG;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST)) *out_raw_value |= HAL_PWR_RESET_RAW_STANDBY;
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_BORRST)) *out_raw_value |= HAL_PWR_RESET_RAW_BROWN_OUT;
    return RET_OK;
}

ret_code_t hal_pwr_port_clear_reset_flags(void) {
    __HAL_RCC_CLEAR_RESET_FLAGS();
    return RET_OK;
}

ret_code_t hal_pwr_port_get_wakeup_reason(uint32_t *out_mask) {
    REQUIRE_RET(out_mask != NULL, PWR_PORT_PARAM(RET_R_NULL_PTR));

    *out_mask = HAL_PWR_WAKEUP_REASON_NONE;
    /* standby 认定为另类的 复位 */
    if (__HAL_PWR_GET_FLAG(PWR_FLAG_SB) != RESET) *out_mask |= HAL_PWR_WAKEUP_REASON_RESET;
    /* 检查RTC时钟是开启的 防止卡死 */
    if ((RCC->BDCR & RCC_BDCR_RTCEN) != 0u) {
        if ((RTC->ISR & RTC_ISR_ALRAF) != 0u) *out_mask |= HAL_PWR_WAKEUP_REASON_RTC_ALARM;
        if ((RTC->ISR & RTC_ISR_WUTF) != 0u) *out_mask |= HAL_PWR_WAKEUP_REASON_RTC_WAKEUP;
    }
    /* 防止RTC 唤醒同时有 RTC pin两种唤醒的 标志位 */
    if ((__HAL_PWR_GET_FLAG(PWR_FLAG_WU) != RESET) && (*out_mask == HAL_PWR_WAKEUP_REASON_NONE))
        *out_mask |= HAL_PWR_WAKEUP_REASON_PIN;
    return RET_OK;
}

ret_code_t hal_pwr_port_clear_wakeup_reason(uint32_t mask) {
    if ((mask & HAL_PWR_WAKEUP_REASON_PIN) != 0u) __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
    if ((mask & HAL_PWR_WAKEUP_REASON_RESET) != 0u) __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SB);
    if ((mask & HAL_PWR_WAKEUP_REASON_RTC_ALARM) != 0u) pwr_clear_rtc_flags(RTC_ISR_ALRAF);
    if ((mask & HAL_PWR_WAKEUP_REASON_RTC_WAKEUP) != 0u) pwr_clear_rtc_flags(RTC_ISR_WUTF);
    return RET_OK;
}

ret_code_t hal_pwr_port_configure_wakeup_source(const hal_pwr_wakeup_source_cfg_t *cfg) {
    REQUIRE_RET(cfg != NULL, PWR_PORT_PARAM(RET_R_NULL_PTR));

    switch (cfg->source) {
        case HAL_PWR_WAKEUP_SOURCE_PIN:
#if defined(PWR_WAKEUP_PIN1)
            REQUIRE_RET((cfg->instance == HAL_PWR_WAKEUP_SOURCE_INSTANCE_DEFAULT) ||
                            (cfg->instance == HAL_PWR_WAKEUP_SOURCE_INSTANCE_1),
                        PWR_PORT_PARAM(RET_R_RANGE_ERR));
            REQUIRE_RET(cfg->option_flags == HAL_PWR_WAKEUP_SOURCE_OPT_NONE,
                        PWR_PORT_PARAM(RET_R_UNSUPPORTED));
            /* 配置唤醒的pin */
            if (cfg->enable)
                HAL_PWR_EnableWakeUpPin(PWR_WAKEUP_PIN1);
            else
                HAL_PWR_DisableWakeUpPin(PWR_WAKEUP_PIN1);
            return RET_OK;
#else
            return PWR_PORT_PARAM(RET_R_UNSUPPORTED);
#endif

        case HAL_PWR_WAKEUP_SOURCE_RTC_ALARM:
        case HAL_PWR_WAKEUP_SOURCE_RTC_WAKEUP:
        case HAL_PWR_WAKEUP_SOURCE_WDG:
            return PWR_PORT_PARAM(RET_R_UNSUPPORTED);

        default:
            return PWR_PORT_PARAM(RET_R_INVALID_ARG);
    }
}

ret_code_t hal_pwr_port_set_mode(hal_pwr_mode_t mode, const hal_pwr_mode_cfg_t *mode_cfg) {
    const uint32_t entry = (mode_cfg != NULL) ? pwr_port_map_entry(mode_cfg->entry) : UINT32_MAX;

    switch (mode) {
        case HAL_PWR_MODE_RUN:
            return RET_OK;

        case HAL_PWR_MODE_SLEEP:
            REQUIRE_RET(mode_cfg != NULL, PWR_PORT_PARAM(RET_R_NULL_PTR));
            REQUIRE_RET(entry != UINT32_MAX, PWR_PORT_PARAM(RET_R_INVALID_ARG));
            HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, entry);
            return RET_OK;

        case HAL_PWR_MODE_STOP: {
            const uint32_t regulator =
                (mode_cfg != NULL) ? pwr_port_map_stop_regulator(mode_cfg->regulator) : UINT32_MAX;

            REQUIRE_RET(mode_cfg != NULL, PWR_PORT_PARAM(RET_R_NULL_PTR));
            REQUIRE_RET(entry != UINT32_MAX, PWR_PORT_PARAM(RET_R_INVALID_ARG));
            REQUIRE_RET(regulator != UINT32_MAX, PWR_PORT_PARAM(RET_R_INVALID_ARG));
            HAL_PWR_EnterSTOPMode(regulator, entry);
            return RET_OK;
        }

        case HAL_PWR_MODE_STANDBY:
            HAL_PWR_EnterSTANDBYMode();
            return RET_OK;

        case HAL_PWR_MODE_SHUTDOWN:
            return PWR_PORT_PARAM(RET_R_UNSUPPORTED);

        default:
            return PWR_PORT_PARAM(RET_R_INVALID_ARG);
    }
}

#endif
