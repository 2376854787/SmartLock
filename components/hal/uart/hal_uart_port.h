#ifndef HAL_UART_PORT_H
#define HAL_UART_PORT_H

#include <stdint.h>

#include "hal_uart.h"

typedef struct hal_uart_port_handle hal_uart_port_handle_t;

/**
 * @brief port 上报的 DMA 增量视图
 * @note 指针仅在当前回调期间有效，HAL 需立即消费
 */
typedef struct {
    const uint8_t* p1;
    uint32_t n1;
    const uint8_t* p2;
    uint32_t n2;
} hal_uart_port_rx_data_t;

typedef enum {
    HAL_UART_PORT_EVT_RX_READY = 1,
    HAL_UART_PORT_EVT_TX_DONE  = 2,
    HAL_UART_PORT_EVT_ERROR    = 3
} hal_uart_port_evt_type_t;

/**
 * @brief port 事件载荷
 */
typedef struct {
    hal_uart_port_evt_type_t type;
    union {
        struct {
            hal_uart_port_rx_data_t data;
        } rx;
        struct {
            uint32_t bytes;
        } tx;
        struct {
            ret_code_t code;
        } err;
    };
} hal_uart_port_event_t;

typedef void (*hal_uart_port_evt_cb_t)(void* user, const hal_uart_port_event_t* evt);

ret_code_t hal_uart_port_init(hal_uart_id_t id, const hal_uart_cfg_t* cfg,
                              hal_uart_port_handle_t** out);
ret_code_t hal_uart_port_deinit(hal_uart_port_handle_t* h);
ret_code_t hal_uart_port_rx_start(hal_uart_port_handle_t* h);
ret_code_t hal_uart_port_send_async(hal_uart_port_handle_t* h, const uint8_t* buf, uint32_t len);
ret_code_t hal_uart_port_set_evt_cb(hal_uart_port_handle_t* h, hal_uart_port_evt_cb_t cb,
                                    void* user);
hal_uart_id_t hal_uart_port_get_id(const hal_uart_port_handle_t* h);
uint32_t hal_uart_port_get_rx_buffer_len(const hal_uart_port_handle_t* h);

#endif /* HAL_UART_PORT_H */
