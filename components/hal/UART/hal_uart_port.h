#ifndef HAL_UART_PORT_H
#define HAL_UART_PORT_H

#include "hal_uart.h"

ret_code_t hal_uart_port_open(hal_uart_id_t id, const hal_uart_cfg_t* cfg, hal_uart_t** out);
ret_code_t hal_uart_port_close(hal_uart_t* h);
ret_code_t hal_uart_port_rx_start(hal_uart_t* h);
ret_code_t hal_uart_port_send_async(hal_uart_t* h, const uint8_t* buf, uint32_t len);
ret_code_t hal_uart_port_read(hal_uart_t* h, uint8_t* out, uint32_t max, uint32_t* nread);
ret_code_t hal_uart_port_read_reserve(hal_uart_t* h, uint32_t want, hal_uart_read_span_t* out,
                                      uint32_t* nread);
ret_code_t hal_uart_port_read_commit(hal_uart_t* h, uint32_t nread);
ret_code_t hal_uart_port_set_evt_cb(hal_uart_t* h, hal_uart_evt_cb_t cb, void* user);
hal_uart_id_t hal_uart_port_get_id(const hal_uart_t* h);

#endif /* HAL_UART_PORT_H */
