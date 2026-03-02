#include <assert.h>
#include <stdio.h>

#include "hal_i2c.h"
#include "hal_i2c_port.h"
#include "osal.h"

static bool g_kernel_running = true;
static ret_code_t g_lock_rc = RET_OK;
static int g_lock_calls = 0;
static bool g_port_inited = false;

void OSAL_enter_critical_ex(osal_crit_state_t *state) {
    if (state != NULL) *state = 0u;
}

void OSAL_exit_critical_ex(osal_crit_state_t state) {
    (void)state;
}

bool OSAL_kernel_is_running(void) {
    return g_kernel_running;
}

ret_code_t OSAL_mutex_create(osal_mutex_t *out, const char *name, bool recursive,
                             bool prio_inherit) {
    (void)name;
    (void)recursive;
    (void)prio_inherit;
    if (out != NULL) *out = 1u;
    return RET_OK;
}

ret_code_t OSAL_mutex_delete(osal_mutex_t mutex) {
    (void)mutex;
    return RET_OK;
}

ret_code_t OSAL_mutex_lock(osal_mutex_t mutex, uint32_t timeout_ms) {
    (void)mutex;
    (void)timeout_ms;
    ++g_lock_calls;
    return g_lock_rc;
}

ret_code_t OSAL_mutex_unlock(osal_mutex_t mutex) {
    (void)mutex;
    return RET_OK;
}

ret_code_t OSAL_sem_create(osal_sem_t *out, const char *name, uint32_t initial_count,
                           uint32_t max_count) {
    (void)out;
    (void)name;
    (void)initial_count;
    (void)max_count;
    return RET_MAKE_RESOURCE(RET_MOD_OSAL, RET_SUB_SYS_NONE, RET_R_NO_MEM);
}

ret_code_t OSAL_sem_delete(osal_sem_t sem) {
    (void)sem;
    return RET_OK;
}

ret_code_t OSAL_sem_take(osal_sem_t sem, uint32_t timeout_ms) {
    (void)sem;
    (void)timeout_ms;
    return RET_OK;
}

ret_code_t OSAL_sem_give(osal_sem_t sem) {
    (void)sem;
    return RET_OK;
}

ret_code_t OSAL_sem_give_from_isr(osal_sem_t sem) {
    (void)sem;
    return RET_OK;
}

ret_code_t OSAL_delay_ms(uint32_t delay_ms) {
    (void)delay_ms;
    return RET_OK;
}

ret_code_t OSAL_msgq_put(osal_msgq_t queue, void *item, uint32_t timeout_ms) {
    (void)queue;
    (void)item;
    (void)timeout_ms;
    return RET_OK;
}

ret_code_t OSAL_thread_flags_set(osal_thread_t thread, osal_flags_t flags) {
    (void)thread;
    (void)flags;
    return RET_OK;
}

ret_code_t hal_i2c_port_init(const hal_i2c_bus_cfg_t *cfg, hal_i2c_port_ctx_t *out) {
    (void)cfg;
    if (out != NULL) {
        *out = (hal_i2c_port_ctx_t){0};
        out->opened = true;
    }
    g_port_inited = true;
    return RET_OK;
}

ret_code_t hal_i2c_port_deinit(hal_i2c_port_ctx_t *ctx) {
    if (ctx != NULL) ctx->opened = false;
    g_port_inited = false;
    return RET_OK;
}

ret_code_t hal_i2c_port_set_evt_cb(hal_i2c_port_ctx_t *ctx, hal_i2c_port_evt_cb_t cb, void *user) {
    if (ctx != NULL) {
        ctx->evt_cb = cb;
        ctx->evt_user = user;
    }
    return RET_OK;
}

ret_code_t hal_i2c_port_apply(hal_i2c_port_ctx_t *ctx, const hal_i2c_dev_cfg_t *dev_cfg,
                              uint32_t bus_default_hz) {
    (void)ctx;
    (void)dev_cfg;
    (void)bus_default_hz;
    return RET_OK;
}

ret_code_t hal_i2c_port_xfer(hal_i2c_port_ctx_t *ctx, const hal_i2c_dev_cfg_t *dev_cfg,
                             const hal_i2c_xfer_t *xfer) {
    (void)ctx;
    (void)dev_cfg;
    (void)xfer;
    return RET_OK;
}

ret_code_t hal_i2c_port_abort(hal_i2c_port_ctx_t *ctx, bool disable_i2c) {
    (void)ctx;
    (void)disable_i2c;
    return RET_OK;
}

void hal_i2c_port_master_tx_cplt_hook(I2C_HandleTypeDef *hi2c) {
    (void)hi2c;
}

void hal_i2c_port_master_rx_cplt_hook(I2C_HandleTypeDef *hi2c) {
    (void)hi2c;
}

void hal_i2c_port_error_hook(I2C_HandleTypeDef *hi2c) {
    (void)hi2c;
}

int main(void) {
    hal_i2c_bus_t *bus = NULL;
    hal_i2c_dev_t *dev = NULL;
    const hal_i2c_bus_cfg_t bus_cfg = {
        .bus_id = HAL_I2C_BUS1,
        .use_dma = true,
        .use_irq = false,
        .default_hz = 400000u,
    };
    const hal_i2c_dev_cfg_t dev_cfg = {
        .dev_addr = 0x50u,
        .addr_mode = HAL_I2C_ADDR_7BIT,
        .max_hz = 400000u,
        .is_master = true,
    };
    const uint8_t tx_data[2] = {0xAAu, 0x55u};
    const hal_i2c_xfer_t xfer = {
        .tx = tx_data,
        .tx_len = 2u,
        .rx = NULL,
        .rx_len = 0u,
        .timeout_ms = 10u,
        .flags = HAL_I2C_XFER_NONE,
    };

    assert(ret_is_ok(hal_i2c_bus_init(&bus_cfg, &bus)));
    assert(g_port_inited);
    assert(ret_is_ok(hal_i2c_dev_attach(bus, &dev_cfg, &dev)));

    g_lock_rc = RET_MAKE_TIMEOUT(RET_MOD_OSAL, RET_SUB_SYS_NONE, RET_R_TIMEOUT);
    assert(hal_i2c_transceive(dev, &xfer) ==
           RET_MAKE_TIMEOUT(RET_MOD_HAL, RET_SUB_HAL_I2C, RET_R_TIMEOUT));
    assert(g_lock_calls == 1);

    assert(ret_is_ok(hal_i2c_dev_detach(dev)));
    assert(ret_is_ok(hal_i2c_bus_deinit(bus)));

    puts("test_hal_i2c: PASS");
    return 0;
}
