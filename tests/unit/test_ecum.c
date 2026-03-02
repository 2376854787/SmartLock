#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "ecum.h"

static int g_suspend_tick_calls = 0;
static int g_resume_tick_calls  = 0;
static int g_hal_pwr_select_config_calls = 0;
static int g_hal_pwr_set_mode_calls = 0;
static int g_restore_clock_calls = 0;
static int g_restore_peripheral_calls = 0;
static int g_application_resume_calls = 0;
static int g_order_index = 0;
static int g_order[16];
static uint8_t g_active_config_set_id = 1u;
static hal_pwr_mode_t g_last_mode = HAL_PWR_MODE_RUN;
static hal_pwr_capability_t g_capability = {0};
static hal_pwr_reset_reason_t g_reset_reason = HAL_PWR_RESET_REASON_POWER_ON;
static uint32_t g_reset_raw_value = HAL_PWR_RESET_RAW_POWER_ON;
static uint32_t g_wakeup_reason = HAL_PWR_WAKEUP_REASON_RTC_WAKEUP;
static hal_pwr_wakeup_source_cfg_t g_wakeup_cfg[HAL_PWR_WAKEUP_SOURCE_COUNT];
static bool g_evt_seen = false;
static ecum_event_t g_last_evt;

enum {
    STEP_SELECT_CONFIG = 1,
    STEP_SUSPEND_TICK,
    STEP_SET_MODE,
    STEP_RESTORE_CLOCK,
    STEP_RESUME_TICK,
    STEP_RESTORE_PERIPHERALS,
    STEP_APPLICATION_RESUME,
};

void HAL_SuspendTick(void) {
    g_order[g_order_index++] = STEP_SUSPEND_TICK;
    ++g_suspend_tick_calls;
}

void HAL_ResumeTick(void) {
    g_order[g_order_index++] = STEP_RESUME_TICK;
    ++g_resume_tick_calls;
}

ret_code_t hal_pwr_init(const hal_pwr_cfg_t *cfg) {
    (void)cfg;
    return RET_OK;
}

ret_code_t hal_pwr_deinit(void) {
    return RET_OK;
}

ret_code_t hal_pwr_get_capability(hal_pwr_capability_t *out) {
    if (out != NULL) *out = g_capability;
    return RET_OK;
}

ret_code_t hal_pwr_select_config_set(uint8_t config_set_id) {
    g_order[g_order_index++] = STEP_SELECT_CONFIG;
    ++g_hal_pwr_select_config_calls;
    g_active_config_set_id = config_set_id;
    return RET_OK;
}

ret_code_t hal_pwr_get_active_config_set(uint8_t *out_config_set_id) {
    if (out_config_set_id != NULL) *out_config_set_id = g_active_config_set_id;
    return RET_OK;
}

ret_code_t hal_pwr_get_mode(hal_pwr_mode_t *out) {
    if (out != NULL) *out = HAL_PWR_MODE_RUN;
    return RET_OK;
}

ret_code_t hal_pwr_set_mode(hal_pwr_mode_t mode) {
    g_order[g_order_index++] = STEP_SET_MODE;
    ++g_hal_pwr_set_mode_calls;
    g_last_mode = mode;
    return RET_OK;
}

ret_code_t hal_pwr_configure_wakeup_source(const hal_pwr_wakeup_source_cfg_t *cfg) {
    if (cfg != NULL) g_wakeup_cfg[cfg->source] = *cfg;
    return RET_OK;
}

ret_code_t hal_pwr_enable_wakeup_source(hal_pwr_wakeup_source_t source) {
    g_wakeup_cfg[source].source = source;
    g_wakeup_cfg[source].enable = true;
    return RET_OK;
}

ret_code_t hal_pwr_disable_wakeup_source(hal_pwr_wakeup_source_t source) {
    g_wakeup_cfg[source].source = source;
    g_wakeup_cfg[source].enable = false;
    return RET_OK;
}

ret_code_t hal_pwr_get_wakeup_source_cfg(hal_pwr_wakeup_source_t source,
                                         hal_pwr_wakeup_source_cfg_t *out_cfg) {
    if (out_cfg != NULL) *out_cfg = g_wakeup_cfg[source];
    return RET_OK;
}

ret_code_t hal_pwr_get_reset_reason(hal_pwr_reset_reason_t *out_reason) {
    if (out_reason != NULL) *out_reason = g_reset_reason;
    return RET_OK;
}

ret_code_t hal_pwr_get_reset_raw_value(uint32_t *out_raw_value) {
    if (out_raw_value != NULL) *out_raw_value = g_reset_raw_value;
    return RET_OK;
}

ret_code_t hal_pwr_clear_reset_flags(void) {
    return RET_OK;
}

ret_code_t hal_pwr_get_wakeup_reason(uint32_t *out_mask) {
    if (out_mask != NULL) *out_mask = g_wakeup_reason;
    return RET_OK;
}

ret_code_t hal_pwr_clear_wakeup_reason(uint32_t mask) {
    (void)mask;
    return RET_OK;
}

static ret_code_t restore_clock(void *user) {
    int *counter = (int *)user;

    g_order[g_order_index++] = STEP_RESTORE_CLOCK;
    ++g_restore_clock_calls;
    ++(*counter);
    return RET_OK;
}

static ret_code_t restore_peripherals(void *user) {
    int *counter = (int *)user;

    g_order[g_order_index++] = STEP_RESTORE_PERIPHERALS;
    ++g_restore_peripheral_calls;
    ++(*counter);
    return RET_OK;
}

static ret_code_t application_resume(void *user) {
    int *counter = (int *)user;

    g_order[g_order_index++] = STEP_APPLICATION_RESUME;
    ++g_application_resume_calls;
    ++(*counter);
    return RET_OK;
}

static void evt_cb(void *user, const ecum_event_t *evt) {
    bool *seen = (bool *)user;

    *seen = true;
    g_evt_seen = true;
    g_last_evt = *evt;
}

static void reset_state(void) {
    g_suspend_tick_calls = 0;
    g_resume_tick_calls  = 0;
    g_hal_pwr_select_config_calls = 0;
    g_hal_pwr_set_mode_calls = 0;
    g_restore_clock_calls = 0;
    g_restore_peripheral_calls = 0;
    g_application_resume_calls = 0;
    g_order_index = 0;
    g_active_config_set_id = 1u;
    g_last_mode = HAL_PWR_MODE_RUN;
    g_capability = (hal_pwr_capability_t){
        .supported_modes_mask = HAL_PWR_MODE_MASK(HAL_PWR_MODE_RUN) |
                                HAL_PWR_MODE_MASK(HAL_PWR_MODE_SLEEP) |
                                HAL_PWR_MODE_MASK(HAL_PWR_MODE_STOP) |
                                HAL_PWR_MODE_MASK(HAL_PWR_MODE_STANDBY),
        .configurable_wakeup_source_mask = HAL_PWR_WAKEUP_SOURCE_MASK(HAL_PWR_WAKEUP_SOURCE_PIN),
        .observable_wakeup_reason_mask = HAL_PWR_WAKEUP_REASON_PIN | HAL_PWR_WAKEUP_REASON_RTC_WAKEUP,
    };
    g_reset_reason = HAL_PWR_RESET_REASON_POWER_ON;
    g_reset_raw_value = HAL_PWR_RESET_RAW_POWER_ON;
    g_wakeup_reason = HAL_PWR_WAKEUP_REASON_RTC_WAKEUP;
    g_evt_seen = false;
    g_last_evt = (ecum_event_t){0};
    g_wakeup_cfg[HAL_PWR_WAKEUP_SOURCE_PIN] = (hal_pwr_wakeup_source_cfg_t){
        .source = HAL_PWR_WAKEUP_SOURCE_PIN,
        .instance = HAL_PWR_WAKEUP_SOURCE_INSTANCE_1,
        .enable = false,
    };
}

static void test_stop_mode_resume_pipeline(void) {
    ecum_t ecum = {0};
    int clock_counter = 0;
    int peripheral_counter = 0;
    int application_counter = 0;
    const hal_pwr_config_set_t config_sets[] = {
        {.config_set_id = 1u},
        {.config_set_id = 2u},
    };
    const hal_pwr_cfg_t pwr_cfg = {
        .config_sets = config_sets,
        .config_set_count = 2u,
        .default_config_set_id = 1u,
    };
    const ecum_cfg_t cfg = {
        .pwr_cfg = &pwr_cfg,
        .mode_policy[HAL_PWR_MODE_STOP] = {
            .suspend_tick = true,
            .select_pwr_config_set = true,
            .pwr_config_set_id = 2u,
            .restore_clock_tree = true,
            .restore_peripherals = true,
            .application_resume = application_resume,
            .application_resume_user = &application_counter,
        },
        .restore_clock_tree = restore_clock,
        .restore_clock_tree_user = &clock_counter,
        .restore_peripherals = restore_peripherals,
        .restore_peripherals_user = &peripheral_counter,
    };

    assert(ret_is_ok(ecum_init(&ecum, &cfg)));
    assert(ret_is_ok(ecum_set_evt_cb(&ecum, evt_cb, &g_evt_seen)));
    assert(ret_is_ok(ecum_request_mode(&ecum, HAL_PWR_MODE_STOP)));

    assert(g_hal_pwr_select_config_calls == 1);
    assert(g_active_config_set_id == 2u);
    assert(g_hal_pwr_set_mode_calls == 1);
    assert(g_last_mode == HAL_PWR_MODE_STOP);
    assert(g_suspend_tick_calls == 1);
    assert(g_resume_tick_calls == 1);
    assert(g_restore_clock_calls == 1);
    assert(g_restore_peripheral_calls == 1);
    assert(g_application_resume_calls == 1);
    assert(clock_counter == 1);
    assert(peripheral_counter == 1);
    assert(application_counter == 1);
    assert(g_order[0] == STEP_SELECT_CONFIG);
    assert(g_order[1] == STEP_SUSPEND_TICK);
    assert(g_order[2] == STEP_SET_MODE);
    assert(g_order[3] == STEP_RESTORE_CLOCK);
    assert(g_order[4] == STEP_RESUME_TICK);
    assert(g_order[5] == STEP_RESTORE_PERIPHERALS);
    assert(g_order[6] == STEP_APPLICATION_RESUME);
    assert(g_evt_seen);
    assert(g_last_evt.resumed_from_mode == HAL_PWR_MODE_STOP);
    assert(g_last_evt.active_pwr_config_set_id == 2u);
    assert(g_last_evt.reset_reason == g_reset_reason);
    assert(g_last_evt.reset_raw_value == g_reset_raw_value);
    assert(g_last_evt.wakeup_reason == g_wakeup_reason);
    assert(g_last_evt.clock_restore_rc == RET_OK);
    assert(g_last_evt.tick_resume_rc == RET_OK);
    assert(g_last_evt.peripheral_restore_rc == RET_OK);
    assert(g_last_evt.application_resume_rc == RET_OK);
    assert(ret_is_ok(ecum_deinit(&ecum)));
}

static void test_wakeup_source_management_and_capability(void) {
    ecum_t ecum = {0};
    hal_pwr_capability_t capability = {0};
    hal_pwr_wakeup_source_cfg_t wakeup_cfg = {0};
    const hal_pwr_config_set_t config_sets[] = {
        {.config_set_id = 1u},
    };
    const hal_pwr_cfg_t pwr_cfg = {
        .config_sets = config_sets,
        .config_set_count = 1u,
        .default_config_set_id = 1u,
    };
    const ecum_cfg_t cfg = {
        .pwr_cfg = &pwr_cfg,
        .mode_policy[HAL_PWR_MODE_SLEEP] = {
            .suspend_tick = true,
        },
    };

    assert(ret_is_ok(ecum_init(&ecum, &cfg)));
    assert(ret_is_ok(ecum_get_pwr_capability(&ecum, &capability)));
    assert((capability.supported_modes_mask & HAL_PWR_MODE_MASK(HAL_PWR_MODE_SLEEP)) != 0u);
    assert((capability.configurable_wakeup_source_mask &
            HAL_PWR_WAKEUP_SOURCE_MASK(HAL_PWR_WAKEUP_SOURCE_PIN)) != 0u);

    wakeup_cfg = (hal_pwr_wakeup_source_cfg_t){
        .source = HAL_PWR_WAKEUP_SOURCE_PIN,
        .instance = HAL_PWR_WAKEUP_SOURCE_INSTANCE_1,
        .enable = true,
    };
    assert(ret_is_ok(ecum_configure_wakeup_source(&ecum, &wakeup_cfg)));
    assert(ret_is_ok(ecum_get_wakeup_source_cfg(&ecum, HAL_PWR_WAKEUP_SOURCE_PIN, &wakeup_cfg)));
    assert(wakeup_cfg.enable);
    assert(ret_is_ok(ecum_disable_wakeup_source(&ecum, HAL_PWR_WAKEUP_SOURCE_PIN)));
    assert(ret_is_ok(ecum_get_wakeup_source_cfg(&ecum, HAL_PWR_WAKEUP_SOURCE_PIN, &wakeup_cfg)));
    assert(!wakeup_cfg.enable);
    assert(ret_is_ok(ecum_enable_wakeup_source(&ecum, HAL_PWR_WAKEUP_SOURCE_PIN)));
    assert(ret_is_ok(ecum_get_wakeup_source_cfg(&ecum, HAL_PWR_WAKEUP_SOURCE_PIN, &wakeup_cfg)));
    assert(wakeup_cfg.enable);
    assert(ret_is_ok(ecum_deinit(&ecum)));
}

int main(void) {
    reset_state();
    test_stop_mode_resume_pipeline();
    reset_state();
    test_wakeup_source_management_and_capability();
    puts("test_ecum: PASS");
    return 0;
}
