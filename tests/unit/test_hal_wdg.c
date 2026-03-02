#include <assert.h>
#include <stdio.h>

#include "hal_wdg.h"
#include "hal_wdg_port.h"

static int g_port_init_calls = 0;
static int g_port_kick_calls = 0;

ret_code_t hal_wdg_port_init(const hal_wdg_cfg_t *cfg) {
    (void)cfg;
    ++g_port_init_calls;
    return RET_OK;
}

ret_code_t hal_wdg_port_kick(void) {
    ++g_port_kick_calls;
    return RET_OK;
}

int main(void) {
    const hal_wdg_cfg_t iwdg_cfg = {
        .mode = HAL_WDG_MODE_IWDG,
        .timeout_ms = 1000u,
        .window_min_ms = 0u,
        .debug_freeze = false,
    };
    const hal_wdg_cfg_t invalid_mode_cfg = {
        .mode = (hal_wdg_mode_t)2,
        .timeout_ms = 1000u,
        .window_min_ms = 0u,
        .debug_freeze = false,
    };

    assert(!hal_wdg_is_inited());
    assert(ret_is_err(hal_wdg_kick()));
    assert(ret_is_err(hal_wdg_init(&invalid_mode_cfg)));
    assert(ret_is_ok(hal_wdg_init(&iwdg_cfg)));
    assert(hal_wdg_is_inited());
    assert(g_port_init_calls == 1);
    assert(ret_is_err(hal_wdg_init(&iwdg_cfg)));
    assert(ret_is_ok(hal_wdg_kick()));
    assert(g_port_kick_calls == 1);

    puts("test_hal_wdg: PASS");
    return 0;
}
