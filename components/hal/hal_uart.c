#include "hal_uart.h"

#include "APP_config.h"

#if defined(ENABLE_HAL_UART)

ret_code_t hal_uart_port_open(hal_uart_id_t id, const hal_uart_cfg_t* cfg, hal_uart_t** out);
ret_code_t hal_uart_port_close(hal_uart_t* h);
ret_code_t hal_uart_port_rx_start(hal_uart_t* h);
ret_code_t hal_uart_port_send_async(hal_uart_t* h, const uint8_t* buf, uint32_t len);
ret_code_t hal_uart_port_read(hal_uart_t* h, uint8_t* out, uint32_t max, uint32_t* nread);
ret_code_t hal_uart_port_set_evt_cb(hal_uart_t* h, hal_uart_evt_cb_t cb, void* user);

ret_code_t hal_uart_open(hal_uart_id_t id, const hal_uart_cfg_t* cfg, hal_uart_t** out) {
    return hal_uart_port_open(id, cfg, out);
}
ret_code_t hal_uart_close(hal_uart_t* h) {
    return hal_uart_port_close(h);
}

ret_code_t hal_uart_rx_start(hal_uart_t* h) {
    return hal_uart_port_rx_start(h);
}

ret_code_t hal_uart_send_async(hal_uart_t* h, const uint8_t* buf, uint32_t len) {
    return hal_uart_port_send_async(h, buf, len);
}

ret_code_t hal_uart_read(hal_uart_t* h, uint8_t* out, uint32_t want, uint32_t* nread) {
    return hal_uart_port_read(h, out, want, nread);
}

ret_code_t hal_uart_set_evt_cb(hal_uart_t* h, hal_uart_evt_cb_t cb, void* user) {
    return hal_uart_port_set_evt_cb(h, cb, user);
}
#else
ret_code_t hal_uart_open(hal_uart_id_t id, const hal_uart_cfg_t* cfg, hal_uart_t** out) {
    (void)id;
    (void)cfg;
    (void)out;
    return RET_E_UNSUPPORTED;
}

ret_code_t hal_uart_close(hal_uart_t* h) {
    (void)h;
    return RET_E_UNSUPPORTED;
}

ret_code_t hal_uart_rx_start(hal_uart_t* h) {
    (void)h;
    return RET_E_UNSUPPORTED;
}

ret_code_t hal_uart_send_async(hal_uart_t* h, const uint8_t* buf, uint32_t len) {
    (void)h;
    (void)buf;
    (void)len;
    return RET_E_UNSUPPORTED;
}

ret_code_t hal_uart_read(hal_uart_t* h, uint8_t* out, uint32_t want, uint32_t* nread) {
    (void)h;
    (void)out;
    (void)want;
    if (nread) *nread = 0;
    return RET_E_UNSUPPORTED;
}

ret_code_t hal_uart_set_evt_cb(hal_uart_t* h, hal_uart_evt_cb_t cb, void* user) {
    (void)h;
    (void)cb;
    (void)user;
    return RET_E_UNSUPPORTED;
}
#endif
