#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "hal_adc.h"
#include "hal_adc_port.h"

static bool g_port_initialized = false;
static uint32_t g_vref_uv = 3300000u;
static hal_adc_controller_cfg_t g_last_controller_cfg;
static hal_adc_port_evt_cb_t g_evt_cb = NULL;
static void *g_evt_user = NULL;
static hal_adc_group_event_t g_last_evt;
static bool g_evt_seen = false;
static uint8_t g_active_group_id = 0u;

static void test_evt_cb(void *user, const hal_adc_group_event_t *evt) {
    (void)user;
    assert(evt != NULL);
    g_last_evt = *evt;
    g_evt_seen = true;
}

static void port_emit_event(hal_adc_group_evt_type_t type, uint8_t group_id,
                            hal_adc_group_class_t group_class, const uint16_t *samples,
                            uint16_t sample_count, bool conversion_active) {
    hal_adc_port_evt_t evt = {
        .type = type,
        .rc_port = RET_OK,
        .group_id = group_id,
        .group_class = group_class,
        .samples = samples,
        .sample_count = sample_count,
        .conversion_active = conversion_active,
    };

    assert(g_evt_cb != NULL);
    g_evt_cb(g_evt_user, &evt);
}

ret_code_t hal_adc_port_get_capability(hal_adc_id_t id, hal_adc_capability_t *out) {
    static const hal_adc_capability_t cap = {
        .supported_resolution_mask =
            HAL_ADC_RES_CAP_6BIT | HAL_ADC_RES_CAP_8BIT | HAL_ADC_RES_CAP_10BIT |
            HAL_ADC_RES_CAP_12BIT,
        .supported_clock_mask =
            HAL_ADC_CLOCK_CAP_DIV2 | HAL_ADC_CLOCK_CAP_DIV4 | HAL_ADC_CLOCK_CAP_DIV6,
        .supported_feature_flags =
            HAL_ADC_FEAT_REGULAR_CONTINUOUS | HAL_ADC_FEAT_REGULAR_DISCONTINUOUS |
            HAL_ADC_FEAT_REGULAR_IT | HAL_ADC_FEAT_REGULAR_DMA |
            HAL_ADC_FEAT_REGULAR_EXTERNAL_TRIGGER | HAL_ADC_FEAT_INJECTED_GROUP |
            HAL_ADC_FEAT_INJECTED_IT | HAL_ADC_FEAT_INJECTED_EXTERNAL_TRIGGER |
            HAL_ADC_FEAT_INJECTED_AUTO,
        .supported_regular_trigger_mask =
            HAL_ADC_REGULAR_TRIGGER_MASK(HAL_ADC_REGULAR_TRIG_SOFTWARE) |
            HAL_ADC_REGULAR_TRIGGER_MASK(HAL_ADC_REGULAR_TRIG_TIM1_CC1),
        .supported_injected_trigger_mask =
            HAL_ADC_INJECTED_TRIGGER_MASK(HAL_ADC_INJECTED_TRIG_SOFTWARE) |
            HAL_ADC_INJECTED_TRIGGER_MASK(HAL_ADC_INJECTED_TRIG_TIM1_CC4),
        .max_regular_group_count = 4u,
        .max_injected_group_count = 2u,
        .max_regular_channels = 16u,
        .max_injected_channels = 4u,
        .sample_time_cycle_table = {3u, 15u, 28u, 56u, 84u, 112u, 144u, 480u},
        .sample_time_cycle_count = HAL_ADC_MAX_SAMPLE_TIME_COUNT,
    };

    if ((id != HAL_ADC_ID_3) || (out == NULL)) {
        return RET_MAKE_RESOURCE(RET_MOD_PORT, RET_SUB_PORT_ADC, RET_R_NO_RESOURCE);
    }
    *out = cap;
    return RET_OK;
}

ret_code_t hal_adc_port_init(hal_adc_id_t id, const hal_adc_controller_cfg_t *cfg) {
    if ((id != HAL_ADC_ID_3) || (cfg == NULL)) {
        return RET_MAKE_RESOURCE(RET_MOD_PORT, RET_SUB_PORT_ADC, RET_R_NO_RESOURCE);
    }
    g_port_initialized = true;
    g_last_controller_cfg = *cfg;
    return RET_OK;
}

ret_code_t hal_adc_port_deinit(hal_adc_id_t id) {
    if (id != HAL_ADC_ID_3) return RET_MAKE_RESOURCE(RET_MOD_PORT, RET_SUB_PORT_ADC, RET_R_NO_RESOURCE);
    g_port_initialized = false;
    g_evt_cb = NULL;
    g_evt_user = NULL;
    return RET_OK;
}

ret_code_t hal_adc_port_calibrate(hal_adc_id_t id) {
    return (id == HAL_ADC_ID_3) ? RET_OK
                                : RET_MAKE_RESOURCE(RET_MOD_PORT, RET_SUB_PORT_ADC, RET_R_NO_RESOURCE);
}

ret_code_t hal_adc_port_get_vref_uv(hal_adc_id_t id, uint32_t *out_uv) {
    if ((id != HAL_ADC_ID_3) || (out_uv == NULL)) {
        return RET_MAKE_RESOURCE(RET_MOD_PORT, RET_SUB_PORT_ADC, RET_R_NO_RESOURCE);
    }
    *out_uv = g_vref_uv;
    return RET_OK;
}

ret_code_t hal_adc_port_read_group(hal_adc_id_t id, const hal_adc_group_cfg_t *group, uint16_t *out,
                                   uint16_t capacity, uint16_t *out_count) {
    uint8_t idx = 0u;

    if ((id != HAL_ADC_ID_3) || (group == NULL) || (out == NULL) || (out_count == NULL)) {
        return RET_MAKE_RESOURCE(RET_MOD_PORT, RET_SUB_PORT_ADC, RET_R_NO_RESOURCE);
    }
    if (capacity < group->channel_count) {
        return RET_MAKE_PARAM(RET_MOD_PORT, RET_SUB_PORT_ADC, RET_R_RANGE_ERR);
    }
    for (idx = 0u; idx < group->channel_count; ++idx) {
        out[idx] = (uint16_t)(100u + idx);
    }
    *out_count = group->channel_count;
    return RET_OK;
}

ret_code_t hal_adc_port_start_group(hal_adc_id_t id, const hal_adc_group_cfg_t *group,
                                    uint16_t *buffer, uint16_t sample_count) {
    if ((id != HAL_ADC_ID_3) || (group == NULL) || (buffer == NULL) ||
        (sample_count < group->channel_count)) {
        return RET_MAKE_PARAM(RET_MOD_PORT, RET_SUB_PORT_ADC, RET_R_RANGE_ERR);
    }
    g_active_group_id = group->group_id;
    return RET_OK;
}

ret_code_t hal_adc_port_stop_group(hal_adc_id_t id, uint8_t group_id,
                                   hal_adc_group_class_t group_class) {
    (void)group_class;
    if ((id != HAL_ADC_ID_3) || (group_id != g_active_group_id)) {
        return RET_MAKE_STATE(RET_MOD_PORT, RET_SUB_PORT_ADC, RET_R_NOT_READY);
    }
    return RET_OK;
}

ret_code_t hal_adc_port_set_evt_cb(hal_adc_id_t id, hal_adc_port_evt_cb_t cb, void *user) {
    if (id != HAL_ADC_ID_3) return RET_MAKE_RESOURCE(RET_MOD_PORT, RET_SUB_PORT_ADC, RET_R_NO_RESOURCE);
    g_evt_cb = cb;
    g_evt_user = user;
    return RET_OK;
}

int main(void) {
    hal_adc_capability_t cap;
    hal_adc_state_t state = HAL_ADC_STATUS_UNINIT;
    hal_adc_group_status_t group_status = HAL_ADC_GROUP_STATUS_UNINIT;
    uint16_t result_buffer[8] = {0};
    uint16_t out[8] = {0};
    uint16_t out_count = 0u;
    static const hal_adc_channel_cfg_t regular_channels[] = {
        {.channel_id = 5u, .sample_time_cycles = 56u, .input_mode = HAL_ADC_INPUT_MODE_SINGLE_ENDED, .input_path = HAL_ADC_INPUT_PATH_EXTERNAL, .offset = 0u},
        {.channel_id = 6u, .sample_time_cycles = 56u, .input_mode = HAL_ADC_INPUT_MODE_SINGLE_ENDED, .input_path = HAL_ADC_INPUT_PATH_EXTERNAL, .offset = 0u},
    };
    static const hal_adc_channel_cfg_t injected_channels[] = {
        {.channel_id = 7u, .sample_time_cycles = 84u, .input_mode = HAL_ADC_INPUT_MODE_SINGLE_ENDED, .input_path = HAL_ADC_INPUT_PATH_EXTERNAL, .offset = 3u},
    };
    static const hal_adc_group_cfg_t groups[] = {
        {
            .group_id = 1u,
            .group_class = HAL_ADC_GROUP_CLASS_REGULAR,
            .transfer_mode = HAL_ADC_TRANSFER_MODE_POLLING,
            .access_mode = HAL_ADC_ACCESS_MODE_SINGLE,
            .eoc_selection = HAL_ADC_EOC_EACH_CONVERSION,
            .continuous_mode = false,
            .discontinuous_mode = false,
            .discontinuous_count = 0u,
            .auto_injected_conversion = false,
            .trigger_edge = HAL_ADC_TRIGGER_EDGE_NONE,
            .regular_trigger_source = HAL_ADC_REGULAR_TRIG_SOFTWARE,
            .injected_trigger_source = HAL_ADC_INJECTED_TRIG_SOFTWARE,
            .channels = regular_channels,
            .channel_count = 2u,
        },
        {
            .group_id = 2u,
            .group_class = HAL_ADC_GROUP_CLASS_REGULAR,
            .transfer_mode = HAL_ADC_TRANSFER_MODE_IT,
            .access_mode = HAL_ADC_ACCESS_MODE_SINGLE,
            .eoc_selection = HAL_ADC_EOC_EACH_CONVERSION,
            .continuous_mode = false,
            .discontinuous_mode = false,
            .discontinuous_count = 0u,
            .auto_injected_conversion = false,
            .trigger_edge = HAL_ADC_TRIGGER_EDGE_NONE,
            .regular_trigger_source = HAL_ADC_REGULAR_TRIG_SOFTWARE,
            .injected_trigger_source = HAL_ADC_INJECTED_TRIG_SOFTWARE,
            .channels = regular_channels,
            .channel_count = 2u,
        },
        {
            .group_id = 3u,
            .group_class = HAL_ADC_GROUP_CLASS_INJECTED,
            .transfer_mode = HAL_ADC_TRANSFER_MODE_IT,
            .access_mode = HAL_ADC_ACCESS_MODE_SINGLE,
            .eoc_selection = HAL_ADC_EOC_SEQUENCE,
            .continuous_mode = false,
            .discontinuous_mode = false,
            .discontinuous_count = 0u,
            .auto_injected_conversion = false,
            .trigger_edge = HAL_ADC_TRIGGER_EDGE_NONE,
            .regular_trigger_source = HAL_ADC_REGULAR_TRIG_SOFTWARE,
            .injected_trigger_source = HAL_ADC_INJECTED_TRIG_SOFTWARE,
            .channels = injected_channels,
            .channel_count = 1u,
        },
    };
    const hal_adc_cfg_t cfg = {
        .controller = {
            .clock_prescaler = HAL_ADC_CLOCK_PCLK_DIV4,
            .resolution = HAL_ADC_RES_12BIT,
            .align = HAL_ADC_ALIGN_RIGHT,
            .reference_uv = 3300000u,
            .low_power_auto_wait = false,
        },
        .groups = groups,
        .group_count = 3u,
    };

    assert(ret_is_ok(hal_adc_get_capability(HAL_ADC_ID_3, &cap)));
    assert((cap.supported_feature_flags & HAL_ADC_FEAT_INJECTED_GROUP) != 0u);
    assert(cap.max_regular_channels == 16u);

    assert(ret_is_ok(hal_adc_init(HAL_ADC_ID_3, &cfg)));
    assert(g_port_initialized);
    assert(g_last_controller_cfg.clock_prescaler == HAL_ADC_CLOCK_PCLK_DIV4);
    assert(ret_is_ok(hal_adc_get_state(HAL_ADC_ID_3, &state)));
    assert(state == HAL_ADC_STATUS_READY);

    assert(ret_is_ok(hal_adc_read_group(HAL_ADC_ID_3, 1u, out, 8u, &out_count)));
    assert(out_count == 2u);
    assert(out[0] == 100u && out[1] == 101u);
    assert(ret_is_ok(hal_adc_get_group_status(HAL_ADC_ID_3, 1u, &group_status)));
    assert(group_status == HAL_ADC_GROUP_STATUS_COMPLETED);

    assert(ret_is_ok(hal_adc_setup_result_buffer(HAL_ADC_ID_3, 2u, result_buffer, 8u)));
    assert(ret_is_ok(hal_adc_set_group_evt_cb(HAL_ADC_ID_3, 2u, test_evt_cb, NULL)));
    assert(ret_is_ok(hal_adc_enable_group_notification(HAL_ADC_ID_3, 2u)));
    assert(ret_is_ok(hal_adc_start_group_conversion(HAL_ADC_ID_3, 2u)));
    assert(ret_is_ok(hal_adc_get_group_status(HAL_ADC_ID_3, 2u, &group_status)));
    assert(group_status == HAL_ADC_GROUP_STATUS_BUSY);
    result_buffer[0] = 123u;
    result_buffer[1] = 124u;
    port_emit_event(HAL_ADC_GROUP_EVT_CONV_CPLT, 2u, HAL_ADC_GROUP_CLASS_REGULAR, result_buffer, 2u, false);
    assert(g_evt_seen);
    assert(g_last_evt.group_id == 2u);
    assert(g_last_evt.sample_count == 2u);
    assert(!g_last_evt.conversion_active);
    assert(ret_is_ok(hal_adc_read_group(HAL_ADC_ID_3, 2u, out, 8u, &out_count)));
    assert(out_count == 2u);
    assert(out[0] == 123u && out[1] == 124u);

    g_evt_seen = false;
    memset(&g_last_evt, 0, sizeof(g_last_evt));
    assert(ret_is_ok(hal_adc_setup_result_buffer(HAL_ADC_ID_3, 3u, result_buffer, 4u)));
    assert(ret_is_ok(hal_adc_set_group_evt_cb(HAL_ADC_ID_3, 3u, test_evt_cb, NULL)));
    assert(ret_is_ok(hal_adc_enable_group_notification(HAL_ADC_ID_3, 3u)));
    assert(ret_is_ok(hal_adc_start_group_conversion(HAL_ADC_ID_3, 3u)));
    result_buffer[0] = 777u;
    port_emit_event(HAL_ADC_GROUP_EVT_CONV_CPLT, 3u, HAL_ADC_GROUP_CLASS_INJECTED, result_buffer, 1u, false);
    assert(g_evt_seen);
    assert(g_last_evt.group_class == HAL_ADC_GROUP_CLASS_INJECTED);
    assert(g_last_evt.samples[0] == 777u);

    assert(ret_is_ok(hal_adc_deinit(HAL_ADC_ID_3)));
    assert(!g_port_initialized);

    puts("test_hal_adc: PASS");
    return 0;
}
