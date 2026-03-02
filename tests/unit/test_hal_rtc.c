#include <assert.h>
#include <stdio.h>

#include "hal_rtc.h"
#include "hal_rtc_port.h"

static bool g_port_initialized = false;
static int g_alarm_irq_calls = 0;
static int g_wakeup_irq_calls = 0;
static bool g_shadow_bypass = false;
static int g_set_time_calls = 0;
static bool g_time_valid = true;
static hal_rtc_capability_t g_capability = {
    .supported_clock_mask = HAL_RTC_CLOCK_CAP_LSI | HAL_RTC_CLOCK_CAP_LSE,
    .feature_flags = HAL_RTC_FEAT_SHADOW_BYPASS | HAL_RTC_FEAT_BACKUP_VALIDITY_FLAG |
                     HAL_RTC_FEAT_ALARM_A | HAL_RTC_FEAT_ALARM_B | HAL_RTC_FEAT_WAKEUP_TIMER |
                     HAL_RTC_FEAT_CURRENT_MONTH_ALARM_ONLY | HAL_RTC_FEAT_SET_TIME_MS_UNSUPPORTED,
};
static hal_rtc_port_evt_cb_t g_evt_cb = NULL;
static void *g_evt_user = NULL;
static hal_rtc_event_t g_last_evt = {0};
static int g_evt_calls = 0;

static void test_evt_cb(void *user, const hal_rtc_event_t *evt) {
    (void)user;
    g_last_evt = *evt;
    ++g_evt_calls;
}

ret_code_t hal_rtc_port_init(const hal_rtc_cfg_t *cfg) {
    (void)cfg;
    g_port_initialized = true;
    return RET_OK;
}

ret_code_t hal_rtc_port_deinit(void) {
    g_port_initialized = false;
    return RET_OK;
}

ret_code_t hal_rtc_port_get_capability(hal_rtc_capability_t *out) {
    if (out != NULL) *out = g_capability;
    return RET_OK;
}

ret_code_t hal_rtc_port_get_clock(hal_rtc_clock_t *out) {
    if (out != NULL) *out = HAL_RTC_CLOCK_LSE;
    return RET_OK;
}

ret_code_t hal_rtc_port_is_time_valid(bool *out_is_valid) {
    if (out_is_valid != NULL) *out_is_valid = g_time_valid;
    return RET_OK;
}

ret_code_t hal_rtc_port_get_time(hal_rtc_time_t *out) {
    if (out != NULL) *out = (hal_rtc_time_t){.year = 2026, .month = 3, .day = 2, .weekday = 1};
    return RET_OK;
}

ret_code_t hal_rtc_port_set_time(const hal_rtc_time_t *time) {
    (void)time;
    ++g_set_time_calls;
    return RET_OK;
}

ret_code_t hal_rtc_port_reset_backup_domain(void) {
    return RET_OK;
}

ret_code_t hal_rtc_port_set_shadow_mode(bool bypass) {
    g_shadow_bypass = bypass;
    return RET_OK;
}

ret_code_t hal_rtc_port_set_alarm(hal_rtc_alarm_id_t id, const hal_rtc_alarm_cfg_t *cfg) {
    (void)id;
    (void)cfg;
    return RET_OK;
}

ret_code_t hal_rtc_port_cancel_alarm(hal_rtc_alarm_id_t id) {
    (void)id;
    return RET_OK;
}

ret_code_t hal_rtc_port_set_wakeup_ms(uint32_t period_ms) {
    (void)period_ms;
    return RET_OK;
}

ret_code_t hal_rtc_port_cancel_wakeup(void) {
    return RET_OK;
}

ret_code_t hal_rtc_port_set_evt_cb(hal_rtc_port_evt_cb_t cb, void *user) {
    g_evt_cb = cb;
    g_evt_user = user;
    return RET_OK;
}

void hal_rtc_port_alarm_irq_handler(void) {
    ++g_alarm_irq_calls;
}

void hal_rtc_port_wakeup_irq_handler(void) {
    ++g_wakeup_irq_calls;
}

int main(void) {
    hal_rtc_clock_t clock = HAL_RTC_CLOCK_LSI;
    hal_rtc_capability_t capability = {0};
    bool time_valid = false;
    const hal_rtc_time_t invalid_date = {
        .year = 2026,
        .month = 2,
        .day = 31,
        .weekday = 2,
        .hour = 8,
        .minute = 0,
        .second = 0,
    };

    hal_rtc_irq_alarm_handler();
    hal_rtc_irq_wakeup_handler();
    assert(g_alarm_irq_calls == 0);
    assert(g_wakeup_irq_calls == 0);

    assert(ret_is_ok(hal_rtc_reset_backup_domain()));
    assert(ret_is_ok(hal_rtc_init(&(hal_rtc_cfg_t){.use_24h_format = true, .clock = HAL_RTC_CLOCK_LSE})));
    assert(g_port_initialized);
    assert(ret_is_ok(hal_rtc_get_capability(&capability)));
    assert((capability.supported_clock_mask & HAL_RTC_CLOCK_CAP_LSE) != 0u);
    assert(ret_is_ok(hal_rtc_get_clock(&clock)));
    assert(clock == HAL_RTC_CLOCK_LSE);
    assert(ret_is_ok(hal_rtc_is_time_valid(&time_valid)));
    assert(time_valid);
    assert(ret_is_ok(hal_rtc_set_shadow_mode(true)));
    assert(g_shadow_bypass);
    assert(ret_is_err(hal_rtc_set_time(&invalid_date)));
    assert(g_set_time_calls == 0);
    assert(ret_is_ok(hal_rtc_set_evt_cb(test_evt_cb, NULL)));

    g_evt_cb(g_evt_user,
             &(hal_rtc_port_evt_t){.type = HAL_RTC_EVT_WAKEUP, .rc_port = RET_OK, .alarm_id = HAL_RTC_ALARM_A});
    assert(g_evt_calls == 1);
    assert(g_last_evt.type == HAL_RTC_EVT_WAKEUP);
    assert(g_last_evt.alarm_id == HAL_RTC_ALARM_NONE);
    assert(ret_is_ok(g_last_evt.rc));

    hal_rtc_irq_alarm_handler();
    hal_rtc_irq_wakeup_handler();
    assert(g_alarm_irq_calls == 1);
    assert(g_wakeup_irq_calls == 1);

    assert(ret_is_ok(hal_rtc_deinit()));
    puts("test_hal_rtc: PASS");
    return 0;
}
