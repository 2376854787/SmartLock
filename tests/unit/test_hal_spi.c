#include <assert.h>
#include <stdio.h>

#include "SchM_Spi.h"
#include "hal_gpio.h"
#include "hal_spi.h"
#include "hal_spi_port.h"
#include "osal.h"

static ret_code_t g_lock_rc = RET_OK;
static int g_lock_calls = 0;
static bool g_port_inited = false;

bool SchM_Spi_KernelIsRunning(void) {
    return true;
}

ret_code_t SchM_Spi_LockCreate(SchM_Spi_LockHandleType *out, const char *name, bool recursive,
                               bool prio_inherit) {
    (void)name;
    (void)recursive;
    (void)prio_inherit;
    if (out != NULL) *out = 1u;
    return RET_OK;
}

void SchM_Spi_LockDelete(SchM_Spi_LockHandleType lock) {
    (void)lock;
}

ret_code_t SchM_Spi_Lock(SchM_Spi_LockHandleType lock, uint32_t timeout_ms) {
    (void)lock;
    (void)timeout_ms;
    ++g_lock_calls;
    return g_lock_rc;
}

void SchM_Spi_Unlock(SchM_Spi_LockHandleType lock) {
    (void)lock;
}

void SchM_Enter_Spi_ExclusiveArea(SchM_Spi_CritStateType *state) {
    if (state != NULL) *state = 0u;
}

void SchM_Exit_Spi_ExclusiveArea(SchM_Spi_CritStateType state) {
    (void)state;
}

void hal_time_delay_us(uint32_t delay_us) {
    (void)delay_us;
}

ret_code_t hal_gpio_acquire(hal_gpio_t **out, uint32_t gpio_id) {
    (void)out;
    (void)gpio_id;
    return RET_OK;
}

ret_code_t hal_gpio_release(hal_gpio_t *gpio) {
    (void)gpio;
    return RET_OK;
}

ret_code_t hal_gpio_config(hal_gpio_t *gpio, const hal_gpio_cfg_t *cfg) {
    (void)gpio;
    (void)cfg;
    return RET_OK;
}

void hal_gpio_write(hal_gpio_t *gpio, hal_gpio_level_t level) {
    (void)gpio;
    (void)level;
}

ret_code_t hal_spi_port_init(const hal_spi_bus_cfg_t *cfg, hal_spi_port_ctx_t *out) {
    (void)cfg;
    if (out != NULL) {
        *out = (hal_spi_port_ctx_t){0};
        out->opened = true;
    }
    g_port_inited = true;
    return RET_OK;
}

ret_code_t hal_spi_port_deinit(hal_spi_port_ctx_t *ctx) {
    if (ctx != NULL) ctx->opened = false;
    g_port_inited = false;
    return RET_OK;
}

ret_code_t hal_spi_port_set_evt_cb(hal_spi_port_ctx_t *ctx, hal_spi_port_evt_cb_t cb, void *user) {
    if (ctx != NULL) {
        ctx->evt_cb = cb;
        ctx->evt_user = user;
    }
    return RET_OK;
}

ret_code_t hal_spi_port_apply(hal_spi_port_ctx_t *ctx, const hal_spi_dev_cfg_t *dev_cfg,
                              uint32_t bus_default_hz) {
    (void)ctx;
    (void)dev_cfg;
    (void)bus_default_hz;
    return RET_OK;
}

ret_code_t hal_spi_port_stream_start(hal_spi_port_ctx_t *ctx, const hal_spi_xfer_t *xfer) {
    (void)ctx;
    (void)xfer;
    return RET_OK;
}

ret_code_t hal_spi_port_stream_stop(hal_spi_port_ctx_t *ctx, bool disable_spi) {
    (void)ctx;
    (void)disable_spi;
    return RET_OK;
}

ret_code_t hal_spi_port_xfer(hal_spi_port_ctx_t *ctx, const hal_spi_xfer_t *xfer) {
    (void)ctx;
    (void)xfer;
    return RET_OK;
}

ret_code_t hal_spi_port_abort(hal_spi_port_ctx_t *ctx, bool disable_spi) {
    (void)ctx;
    (void)disable_spi;
    return RET_OK;
}

ret_code_t hal_spi_port_wait_idle(const hal_spi_port_ctx_t *ctx, uint32_t spin_max) {
    (void)ctx;
    (void)spin_max;
    return RET_OK;
}

void hal_spi_port_irq_dispatch_hook(IRQn_Type irqn) {
    (void)irqn;
}

void hal_spi_port_tx_half_cplt_hook(SPI_HandleTypeDef *hspi) {
    (void)hspi;
}

void hal_spi_port_rx_half_cplt_hook(SPI_HandleTypeDef *hspi) {
    (void)hspi;
}

void hal_spi_port_tx_cplt_hook(SPI_HandleTypeDef *hspi) {
    (void)hspi;
}

void hal_spi_port_rx_cplt_hook(SPI_HandleTypeDef *hspi) {
    (void)hspi;
}

void hal_spi_port_txrx_cplt_hook(SPI_HandleTypeDef *hspi) {
    (void)hspi;
}

void hal_spi_port_txrx_half_cplt_hook(SPI_HandleTypeDef *hspi) {
    (void)hspi;
}

void hal_spi_port_error_hook(SPI_HandleTypeDef *hspi) {
    (void)hspi;
}

int main(void) {
    hal_spi_bus_t *bus = NULL;
    hal_spi_dev_t *dev = NULL;
    const uint8_t tx_data[2] = {0x12u, 0x34u};
    const hal_spi_bus_cfg_t bus_cfg = {
        .bus_id = HAL_SPI_BUS1,
        .use_dma = true,
        .use_irq = false,
        .default_hz = 1000000u,
    };
    const hal_spi_dev_cfg_t dev_cfg = {
        .mode = HAL_SPI_MODE0,
        .bit_order = HAL_SPI_BITORDER_MSB,
        .frame_bits = HAL_SPI_FRAME_8,
        .max_hz = 1000000u,
        .cs_type = HAL_SPI_CS_HW,
        .is_master = true,
        .dir = HAL_SPI_DIR_2LINES,
    };
    const hal_spi_xfer_t xfer = {
        .tx = tx_data,
        .rx = NULL,
        .len = 2u,
        .timeout_ms = 5u,
        .flags = HAL_SPI_XFER_NONE,
    };

    assert(ret_is_ok(hal_spi_bus_init(&bus_cfg, &bus)));
    assert(g_port_inited);
    assert(ret_is_ok(hal_spi_dev_attach(bus, &dev_cfg, &dev)));

    g_lock_rc = RET_MAKE_RESOURCE(RET_MOD_OSAL, RET_SUB_SYS_NONE, RET_R_NO_MEM);
    assert(hal_spi_transceive(dev, &xfer) ==
           RET_MAKE_RESOURCE(RET_MOD_HAL, RET_SUB_HAL_SPI, RET_R_NO_MEM));
    assert(g_lock_calls == 1);

    assert(ret_is_ok(hal_spi_dev_detach(dev)));
    assert(ret_is_ok(hal_spi_bus_deinit(bus)));

    puts("test_hal_spi: PASS");
    return 0;
}
