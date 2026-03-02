#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "rtc_manager.h"

static int g_init_calls = 0;
static int g_deinit_calls = 0;
static int g_reset_backup_calls = 0;
static int g_set_shadow_calls = 0;
static int g_set_time_calls = 0;
static hal_rtc_clock_t g_last_init_clock = HAL_RTC_CLOCK_LSI;
static bool g_last_shadow_bypass = false;
static hal_rtc_time_t g_last_time;
static ret_code_t g_lse_init_rc = RET_OK;
static ret_code_t g_lsi_init_rc = RET_OK;
static hal_rtc_clock_t g_active_clock = HAL_RTC_CLOCK_LSI;
static bool g_time_valid = false;
static hal_rtc_capability_t g_capability = {
    .supported_clock_mask = HAL_RTC_CLOCK_CAP_LSI | HAL_RTC_CLOCK_CAP_LSE,
    .feature_flags = HAL_RTC_FEAT_SHADOW_BYPASS | HAL_RTC_FEAT_BACKUP_VALIDITY_FLAG,
};
static rtc_manager_ctx_t g_ctx;

enum {
    TEST_CFGSET_BOOT = 0,
    TEST_CFGSET_KEEP = 1,
};

ret_code_t hal_rtc_init(const hal_rtc_cfg_t *cfg) {
    ++g_init_calls;
    g_last_init_clock = cfg->clock;
    if (cfg->clock == HAL_RTC_CLOCK_LSE) {
        if (ret_is_ok(g_lse_init_rc)) g_active_clock = HAL_RTC_CLOCK_LSE;
        return g_lse_init_rc;
    }
    if (ret_is_ok(g_lsi_init_rc)) g_active_clock = HAL_RTC_CLOCK_LSI;
    return g_lsi_init_rc;
}

ret_code_t hal_rtc_deinit(void) {
    ++g_deinit_calls;
    return RET_OK;
}

ret_code_t hal_rtc_get_status(hal_rtc_status_t *out) {
    if (out != NULL) *out = HAL_RTC_STATUS_READY;
    return RET_OK;
}

ret_code_t hal_rtc_get_capability(hal_rtc_capability_t *out) {
    if (out != NULL) *out = g_capability;
    return RET_OK;
}

ret_code_t hal_rtc_get_clock(hal_rtc_clock_t *out) {
    if (out != NULL) *out = g_active_clock;
    return RET_OK;
}

ret_code_t hal_rtc_is_time_valid(bool *out_is_valid) {
    if (out_is_valid != NULL) *out_is_valid = g_time_valid;
    return RET_OK;
}

ret_code_t hal_rtc_get_time(hal_rtc_time_t *out) {
    if (out != NULL) *out = g_last_time;
    return RET_OK;
}

ret_code_t hal_rtc_set_time(const hal_rtc_time_t *time) {
    ++g_set_time_calls;
    g_last_time = *time;
    return RET_OK;
}

ret_code_t hal_rtc_reset_backup_domain(void) {
    ++g_reset_backup_calls;
    return RET_OK;
}

ret_code_t hal_rtc_set_shadow_mode(bool bypass) {
    ++g_set_shadow_calls;
    g_last_shadow_bypass = bypass;
    return RET_OK;
}

ret_code_t hal_rtc_set_alarm(hal_rtc_alarm_id_t id, const hal_rtc_alarm_cfg_t *cfg) {
    (void)id;
    (void)cfg;
    return RET_OK;
}

ret_code_t hal_rtc_cancel_alarm(hal_rtc_alarm_id_t id) {
    (void)id;
    return RET_OK;
}

ret_code_t hal_rtc_set_wakeup_ms(uint32_t period_ms) {
    (void)period_ms;
    return RET_OK;
}

ret_code_t hal_rtc_cancel_wakeup(void) {
    return RET_OK;
}

ret_code_t hal_rtc_set_evt_cb(hal_rtc_evt_cb_t cb, void *user) {
    (void)cb;
    (void)user;
    return RET_OK;
}

void hal_rtc_irq_alarm_handler(void) {}
void hal_rtc_irq_wakeup_handler(void) {}

static void reset_state(void) {
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_init_calls = 0;
    g_deinit_calls = 0;
    g_reset_backup_calls = 0;
    g_set_shadow_calls = 0;
    g_set_time_calls = 0;
    g_last_init_clock = HAL_RTC_CLOCK_LSI;
    g_last_shadow_bypass = false;
    g_last_time = (hal_rtc_time_t){0};
    g_lse_init_rc = RET_OK;
    g_lsi_init_rc = RET_OK;
    g_active_clock = HAL_RTC_CLOCK_LSI;
    g_time_valid = false;
    g_capability = (hal_rtc_capability_t){
        .supported_clock_mask = HAL_RTC_CLOCK_CAP_LSI | HAL_RTC_CLOCK_CAP_LSE,
        .feature_flags = HAL_RTC_FEAT_SHADOW_BYPASS | HAL_RTC_FEAT_BACKUP_VALIDITY_FLAG,
    };
}

static rtc_manager_config_t make_config(const rtc_manager_cfg_t *sets, uint8_t count) {
    rtc_manager_config_t config = {
        .config_sets = sets,
        .config_set_count = count,
    };

    return config;
}

static void test_fallback_and_default_time(void) {
    const rtc_manager_cfg_t cfg_sets[] = {
        [TEST_CFGSET_BOOT] =
            {
                .use_24h_format     = true,
                .keep_backup_domain = false,
                .bypass_shadow      = true,
                .init_default_time  = true,
                .default_time       = {.year = 2026, .month = 3, .day = 2, .weekday = 1},
                .clock_policy       = RTC_MANAGER_CLOCK_LSE_THEN_LSI,
            },
    };
    const rtc_manager_config_t config = make_config(cfg_sets, (uint8_t)(sizeof(cfg_sets) / sizeof(cfg_sets[0])));
    rtc_manager_cfgset_id_t active_cfgset = 0xFFu;
    hal_rtc_clock_t clock = HAL_RTC_CLOCK_LSI;

    g_lse_init_rc = RET_MAKE_IO(RET_MOD_HAL, RET_SUB_HAL_RTC, RET_R_IO);
    g_lsi_init_rc = RET_OK;
    g_time_valid = false;

    assert(ret_is_ok(rtc_manager_init(&g_ctx, &config, TEST_CFGSET_BOOT)));
    assert(g_ctx.initialized);
    assert(g_reset_backup_calls == 1);
    assert(g_init_calls == 2);
    assert(g_set_shadow_calls == 1);
    assert(g_last_shadow_bypass);
    assert(g_set_time_calls == 1);
    assert(g_last_time.year == 2026);
    assert(ret_is_ok(rtc_manager_get_active_cfgset(&g_ctx, &active_cfgset)));
    assert(active_cfgset == TEST_CFGSET_BOOT);
    assert(ret_is_ok(rtc_manager_get_clock(&g_ctx, &clock)));
    assert(clock == HAL_RTC_CLOCK_LSI);
    assert(ret_is_ok(rtc_manager_deinit(&g_ctx)));
    assert(g_deinit_calls == 1);
}

static void test_keep_backup_skips_reset_and_default_time(void) {
    const rtc_manager_cfg_t cfg_sets[] = {
        [TEST_CFGSET_KEEP] =
            {
                .use_24h_format     = true,
                .keep_backup_domain = true,
                .bypass_shadow      = false,
                .init_default_time  = true,
                .default_time       = {.year = 2026, .month = 3, .day = 2, .weekday = 1},
                .clock_policy       = RTC_MANAGER_CLOCK_LSI_ONLY,
            },
    };
    const rtc_manager_config_t config = make_config(cfg_sets, (uint8_t)(sizeof(cfg_sets) / sizeof(cfg_sets[0])));

    g_time_valid = true;
    assert(ret_is_ok(rtc_manager_init(&g_ctx, &config, TEST_CFGSET_KEEP)));
    assert(g_reset_backup_calls == 0);
    assert(g_init_calls == 1);
    assert(g_set_shadow_calls == 1);
    assert(!g_last_shadow_bypass);
    assert(g_set_time_calls == 0);
    assert(ret_is_ok(rtc_manager_deinit(&g_ctx)));
}

static void test_keep_backup_but_invalid_time_writes_default_time(void) {
    const rtc_manager_cfg_t cfg_sets[] = {
        [TEST_CFGSET_KEEP] =
            {
                .use_24h_format     = true,
                .keep_backup_domain = true,
                .bypass_shadow      = false,
                .init_default_time  = true,
                .default_time       = {.year = 2026, .month = 3, .day = 2, .weekday = 1},
                .clock_policy       = RTC_MANAGER_CLOCK_LSI_ONLY,
            },
    };
    const rtc_manager_config_t config = make_config(cfg_sets, (uint8_t)(sizeof(cfg_sets) / sizeof(cfg_sets[0])));

    g_time_valid = false;
    assert(ret_is_ok(rtc_manager_init(&g_ctx, &config, TEST_CFGSET_KEEP)));
    assert(g_reset_backup_calls == 0);
    assert(g_set_time_calls == 1);
    assert(g_last_time.year == 2026);
    assert(ret_is_ok(rtc_manager_deinit(&g_ctx)));
}

static void test_invalid_default_time_is_rejected_early(void) {
    const rtc_manager_cfg_t cfg_sets[] = {
        [TEST_CFGSET_BOOT] =
            {
                .use_24h_format     = true,
                .keep_backup_domain = false,
                .bypass_shadow      = false,
                .init_default_time  = true,
                .default_time       = {.year = 2026, .month = 2, .day = 31, .weekday = 1},
                .clock_policy       = RTC_MANAGER_CLOCK_LSI_ONLY,
            },
    };
    const rtc_manager_config_t config = make_config(cfg_sets, (uint8_t)(sizeof(cfg_sets) / sizeof(cfg_sets[0])));

    assert(ret_is_err(rtc_manager_init(&g_ctx, &config, TEST_CFGSET_BOOT)));
    assert(g_reset_backup_calls == 0);
    assert(g_init_calls == 0);
    assert(g_set_shadow_calls == 0);
    assert(g_set_time_calls == 0);
}

static void test_keep_backup_requires_validity_flag(void) {
    const rtc_manager_cfg_t cfg_sets[] = {
        [TEST_CFGSET_KEEP] =
            {
                .use_24h_format     = true,
                .keep_backup_domain = true,
                .bypass_shadow      = false,
                .init_default_time  = true,
                .default_time       = {.year = 2026, .month = 3, .day = 2, .weekday = 1},
                .clock_policy       = RTC_MANAGER_CLOCK_LSI_ONLY,
            },
    };
    const rtc_manager_config_t config = make_config(cfg_sets, (uint8_t)(sizeof(cfg_sets) / sizeof(cfg_sets[0])));

    g_capability.feature_flags = HAL_RTC_FEAT_SHADOW_BYPASS;

    assert(ret_is_err(rtc_manager_init(&g_ctx, &config, TEST_CFGSET_KEEP)));
    assert(g_reset_backup_calls == 0);
    assert(g_init_calls == 0);
    assert(g_set_time_calls == 0);
}

static void test_invalid_cfgset_is_rejected_early(void) {
    const rtc_manager_cfg_t cfg_sets[] = {
        [TEST_CFGSET_BOOT] =
            {
                .use_24h_format     = true,
                .keep_backup_domain = false,
                .bypass_shadow      = false,
                .init_default_time  = false,
                .clock_policy       = RTC_MANAGER_CLOCK_LSI_ONLY,
            },
    };
    const rtc_manager_config_t config = make_config(cfg_sets, (uint8_t)(sizeof(cfg_sets) / sizeof(cfg_sets[0])));

    assert(ret_is_err(rtc_manager_init(&g_ctx, &config, 3u)));
    assert(g_init_calls == 0);
    assert(g_reset_backup_calls == 0);
}

static void test_second_ctx_is_blocked_while_active(void) {
    const rtc_manager_cfg_t cfg_sets[] = {
        [TEST_CFGSET_BOOT] =
            {
                .use_24h_format     = true,
                .keep_backup_domain = false,
                .bypass_shadow      = false,
                .init_default_time  = false,
                .clock_policy       = RTC_MANAGER_CLOCK_LSI_ONLY,
            },
    };
    const rtc_manager_config_t config = make_config(cfg_sets, (uint8_t)(sizeof(cfg_sets) / sizeof(cfg_sets[0])));
    rtc_manager_ctx_t other_ctx = {0};

    assert(ret_is_ok(rtc_manager_init(&g_ctx, &config, TEST_CFGSET_BOOT)));
    assert(ret_is_err(rtc_manager_init(&other_ctx, &config, TEST_CFGSET_BOOT)));
    assert(ret_is_ok(rtc_manager_deinit(&g_ctx)));
}

int main(void) {
    reset_state();
    test_fallback_and_default_time();
    reset_state();
    test_keep_backup_skips_reset_and_default_time();
    reset_state();
    test_keep_backup_but_invalid_time_writes_default_time();
    reset_state();
    test_invalid_default_time_is_rejected_early();
    reset_state();
    test_keep_backup_requires_validity_flag();
    reset_state();
    test_invalid_cfgset_is_rejected_early();
    reset_state();
    test_second_ctx_is_blocked_while_active();
    puts("test_rtc_manager: PASS");
    return 0;
}
