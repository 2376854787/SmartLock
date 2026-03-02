#include <assert.h>
#include <stdio.h>

#include "hal_pwr.h"

static bool g_port_initialized = false;
static int g_apply_config_set_calls = 0;
static int g_configure_wakeup_calls = 0;
static int g_clear_reset_calls = 0;
static int g_clear_wakeup_calls = 0;
static int g_port_set_mode_calls = 0;
static uint8_t g_active_config_set_id = 0u;
static hal_pwr_mode_t g_last_mode = HAL_PWR_MODE_RUN;
static hal_pwr_mode_cfg_t g_last_mode_cfg = {0};
static hal_pwr_capability_t g_capability = {0};
static hal_pwr_wakeup_source_cfg_t g_last_wakeup_cfg[HAL_PWR_WAKEUP_SOURCE_COUNT];

ret_code_t hal_pwr_port_init(void) {
    g_port_initialized = true;
    return RET_OK;
}

ret_code_t hal_pwr_port_deinit(void) {
    g_port_initialized = false;
    return RET_OK;
}

ret_code_t hal_pwr_port_apply_config_set(const hal_pwr_config_set_t *cfg_set) {
    ++g_apply_config_set_calls;
    g_active_config_set_id = cfg_set->config_set_id;
    return RET_OK;
}

ret_code_t hal_pwr_port_get_capability(hal_pwr_capability_t *out) {
    if (out != NULL) *out = g_capability;
    return RET_OK;
}

ret_code_t hal_pwr_port_get_reset_raw_value(uint32_t *out_raw_value) {
    if (out_raw_value != NULL) *out_raw_value = HAL_PWR_RESET_RAW_POWER_ON | HAL_PWR_RESET_RAW_BROWN_OUT;
    return RET_OK;
}

ret_code_t hal_pwr_port_clear_reset_flags(void) {
    ++g_clear_reset_calls;
    return RET_OK;
}

ret_code_t hal_pwr_port_get_wakeup_reason(uint32_t *out_mask) {
    if (out_mask != NULL) *out_mask = HAL_PWR_WAKEUP_REASON_PIN;
    return RET_OK;
}

ret_code_t hal_pwr_port_clear_wakeup_reason(uint32_t mask) {
    (void)mask;
    ++g_clear_wakeup_calls;
    return RET_OK;
}

ret_code_t hal_pwr_port_configure_wakeup_source(const hal_pwr_wakeup_source_cfg_t *cfg) {
    ++g_configure_wakeup_calls;
    if (cfg != NULL) g_last_wakeup_cfg[cfg->source] = *cfg;
    return RET_OK;
}

ret_code_t hal_pwr_port_set_mode(hal_pwr_mode_t mode, const hal_pwr_mode_cfg_t *mode_cfg) {
    ++g_port_set_mode_calls;
    g_last_mode = mode;
    if (mode_cfg != NULL) g_last_mode_cfg = *mode_cfg;
    return RET_OK;
}

static void reset_state(void) {
    g_port_initialized = false;
    g_apply_config_set_calls = 0;
    g_configure_wakeup_calls = 0;
    g_clear_reset_calls = 0;
    g_clear_wakeup_calls = 0;
    g_port_set_mode_calls = 0;
    g_active_config_set_id = 0u;
    g_last_mode = HAL_PWR_MODE_RUN;
    g_last_mode_cfg = (hal_pwr_mode_cfg_t){0};
    g_capability = (hal_pwr_capability_t){
        .supported_modes_mask = HAL_PWR_MODE_MASK(HAL_PWR_MODE_RUN) |
                                HAL_PWR_MODE_MASK(HAL_PWR_MODE_SLEEP) |
                                HAL_PWR_MODE_MASK(HAL_PWR_MODE_STOP) |
                                HAL_PWR_MODE_MASK(HAL_PWR_MODE_STANDBY),
        .configurable_wakeup_source_mask = HAL_PWR_WAKEUP_SOURCE_MASK(HAL_PWR_WAKEUP_SOURCE_PIN),
        .observable_wakeup_reason_mask = HAL_PWR_WAKEUP_REASON_PIN | HAL_PWR_WAKEUP_REASON_RTC_WAKEUP,
        .mode_entry_cap =
            {
                [HAL_PWR_MODE_SLEEP] = HAL_PWR_MODE_ENTRY_CAP_WFI | HAL_PWR_MODE_ENTRY_CAP_WFE,
                [HAL_PWR_MODE_STOP] = HAL_PWR_MODE_ENTRY_CAP_WFI | HAL_PWR_MODE_ENTRY_CAP_WFE,
            },
        .regulator_cap =
            {
                [HAL_PWR_MODE_SLEEP] = HAL_PWR_REGULATOR_CAP_MAIN,
                [HAL_PWR_MODE_STOP] = HAL_PWR_REGULATOR_CAP_MAIN | HAL_PWR_REGULATOR_CAP_LOW_POWER,
            },
    };
    g_last_wakeup_cfg[HAL_PWR_WAKEUP_SOURCE_PIN] = (hal_pwr_wakeup_source_cfg_t){
        .source = HAL_PWR_WAKEUP_SOURCE_PIN,
        .instance = HAL_PWR_WAKEUP_SOURCE_INSTANCE_1,
        .enable = false,
    };
}

int main(void) {
    hal_pwr_status_t status = HAL_PWR_STATUS_UNINIT;
    hal_pwr_mode_t current_mode = HAL_PWR_MODE_SHUTDOWN;
    hal_pwr_capability_t capability = {0};
    hal_pwr_reset_reason_t reset_reason = HAL_PWR_RESET_REASON_UNDEFINED;
    uint32_t reset_raw_value = 0u;
    uint32_t wakeup_reason = 0u;
    uint8_t active_config_set_id = 0u;
    bool init_matches = false;
    hal_pwr_wakeup_source_cfg_t wakeup_cfg = {0};
    const hal_pwr_config_set_t config_sets[] = {
        {
            .config_set_id = 1u,
            .mode_cfg =
                {
                    [HAL_PWR_MODE_SLEEP] =
                        {
                            .entry = HAL_PWR_MODE_ENTRY_WFI,
                            .regulator = HAL_PWR_REGULATOR_MAIN,
                        },
                    [HAL_PWR_MODE_STOP] =
                        {
                            .entry = HAL_PWR_MODE_ENTRY_WFE,
                            .regulator = HAL_PWR_REGULATOR_LOW_POWER,
                        },
                },
            .wakeup_source_cfg =
                {
                    [HAL_PWR_WAKEUP_SOURCE_PIN] =
                        {
                            .instance = HAL_PWR_WAKEUP_SOURCE_INSTANCE_1,
                            .enable = true,
                        },
                },
        },
        {
            .config_set_id = 2u,
            .mode_cfg =
                {
                    [HAL_PWR_MODE_SLEEP] =
                        {
                            .entry = HAL_PWR_MODE_ENTRY_WFE,
                            .regulator = HAL_PWR_REGULATOR_MAIN,
                        },
                    [HAL_PWR_MODE_STOP] =
                        {
                            .entry = HAL_PWR_MODE_ENTRY_WFI,
                            .regulator = HAL_PWR_REGULATOR_MAIN,
                        },
                },
            .wakeup_source_cfg =
                {
                    [HAL_PWR_WAKEUP_SOURCE_PIN] =
                        {
                            .instance = HAL_PWR_WAKEUP_SOURCE_INSTANCE_1,
                            .enable = false,
                        },
                },
        },
    };
    const hal_pwr_cfg_t cfg = {
        .config_sets = config_sets,
        .config_set_count = 2u,
        .default_config_set_id = 1u,
    };

    reset_state();

    assert(ret_is_ok(hal_pwr_init(&cfg)));
    assert(g_port_initialized);
    assert(g_apply_config_set_calls == 1);
    assert(g_active_config_set_id == 1u);
    assert(g_configure_wakeup_calls == 1);
    assert(g_last_wakeup_cfg[HAL_PWR_WAKEUP_SOURCE_PIN].enable);
    assert(ret_is_ok(hal_pwr_init_check(&cfg, &init_matches)));
    assert(init_matches);
    assert(ret_is_ok(hal_pwr_get_status(&status)));
    assert(status == HAL_PWR_STATUS_READY);
    assert(ret_is_ok(hal_pwr_get_capability(&capability)));
    assert((capability.supported_modes_mask & HAL_PWR_MODE_MASK(HAL_PWR_MODE_STOP)) != 0u);
    assert(ret_is_ok(hal_pwr_get_active_config_set(&active_config_set_id)));
    assert(active_config_set_id == 1u);
    assert(ret_is_ok(hal_pwr_get_mode(&current_mode)));
    assert(current_mode == HAL_PWR_MODE_RUN);
    assert(ret_is_ok(hal_pwr_get_reset_reason(&reset_reason)));
    assert(reset_reason == HAL_PWR_RESET_REASON_BROWN_OUT);
    assert(ret_is_ok(hal_pwr_get_reset_raw_value(&reset_raw_value)));
    assert(reset_raw_value == (HAL_PWR_RESET_RAW_POWER_ON | HAL_PWR_RESET_RAW_BROWN_OUT));
    assert(ret_is_ok(hal_pwr_get_wakeup_reason(&wakeup_reason)));
    assert(wakeup_reason == HAL_PWR_WAKEUP_REASON_PIN);

    assert(ret_is_ok(hal_pwr_select_config_set(2u)));
    assert(g_apply_config_set_calls == 2);
    assert(g_active_config_set_id == 2u);
    assert(!g_last_wakeup_cfg[HAL_PWR_WAKEUP_SOURCE_PIN].enable);

    wakeup_cfg = (hal_pwr_wakeup_source_cfg_t){
        .source = HAL_PWR_WAKEUP_SOURCE_PIN,
        .instance = HAL_PWR_WAKEUP_SOURCE_INSTANCE_1,
        .enable = true,
    };
    assert(ret_is_ok(hal_pwr_configure_wakeup_source(&wakeup_cfg)));
    assert(ret_is_ok(hal_pwr_get_wakeup_source_cfg(HAL_PWR_WAKEUP_SOURCE_PIN, &wakeup_cfg)));
    assert(wakeup_cfg.enable);
    assert(ret_is_ok(hal_pwr_disable_wakeup_source(HAL_PWR_WAKEUP_SOURCE_PIN)));
    assert(ret_is_ok(hal_pwr_get_wakeup_source_cfg(HAL_PWR_WAKEUP_SOURCE_PIN, &wakeup_cfg)));
    assert(!wakeup_cfg.enable);

    assert(ret_is_ok(hal_pwr_set_mode(HAL_PWR_MODE_STOP)));
    assert(g_port_set_mode_calls == 1);
    assert(g_last_mode == HAL_PWR_MODE_STOP);
    assert(g_last_mode_cfg.entry == HAL_PWR_MODE_ENTRY_WFI);
    assert(g_last_mode_cfg.regulator == HAL_PWR_REGULATOR_MAIN);
    assert(ret_is_ok(hal_pwr_get_mode(&current_mode)));
    assert(current_mode == HAL_PWR_MODE_RUN);

    assert(ret_is_ok(hal_pwr_clear_reset_flags()));
    assert(g_clear_reset_calls == 1);
    assert(ret_is_ok(hal_pwr_clear_wakeup_reason(HAL_PWR_WAKEUP_REASON_PIN)));
    assert(g_clear_wakeup_calls == 1);
    assert(ret_is_ok(hal_pwr_deinit()));
    assert(!g_port_initialized);

    puts("test_hal_pwr: PASS");
    return 0;
}
