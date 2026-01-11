#ifndef HAL_UART_H
#define HAL_UART_H
#include <stdint.h>
#include <stddef.h>

#include "ret_code.h"

/* 串口号定义 */
typedef enum {
    HAL_UART_1 = 0,
    HAL_UART_2,
    HAL_UART_3,
    HAL_UART_4,
    HAL_UART_5,
    HAL_UART_6,
    HAL_UART_7,
    HAL_UART_8,
    HAL_UART_9,
    HAL_UART_10,
    HAL_UART_MAX
} hal_uart_id_t;

/* 句柄定义：应用层只持有指针 */
typedef void *hal_uart_handle_t;

/* UART 事件类型 */
typedef enum {
    HAL_UART_EVENT_RX_DATA, // 收到数据（通常配合 DMA IDLE 使用）
    HAL_UART_EVENT_TX_COMPLETE, // 异步发送完成
    HAL_UART_EVENT_ERROR // 发生错误
} hal_uart_event_t;

/**
 * @brief 接收回调函数指针
 * @param user_ctx
 * @param data 接收到的数据指针
 * @param len 数据长度
 */
typedef void (*hal_uart_rx_cb_t)(void *user_ctx, uint8_t *data, uint16_t len);

/**
 * @brief 获取 UART 实例句柄
 * @param id 串口索引（如 0 代表 UART1）
 * @return 实例句柄，失败返回 NULL
 */
hal_uart_handle_t hal_uart_get_instance(uint8_t id);

/**
 * @brief 发送数据（阻塞/半阻塞）
 * @note 提供超时机制
 */
ret_code_t hal_uart_send(hal_uart_handle_t handle, const uint8_t *data, uint16_t len, uint32_t timeout_ms);

/**
 * @brief 异步发送（非阻塞）
 * @return HAL_STATUS_OK: 已启动发送；HAL_STATUS_BUSY: 上一包还没发完
 * @note 当发送真正完成后，会触发 HAL_UART_EVENT_TX_COMPLETE 回调
 */
ret_code_t hal_uart_send_async(hal_uart_handle_t handle, const uint8_t *data, uint16_t len);


/**
 * @brief 启动异步接收（非阻塞）
 * @param handle 句柄
 * @param rx_buffer 接收缓冲区（通常由上层提供）
 * @param max_len   缓冲区最大长度
 * @note 底层通常开启 DMA + IDLE 中断。只要收到一串连续数据且总线空闲，就触发 RX_DATA 回调。
 */
ret_code_t hal_uart_read_async(hal_uart_handle_t handle, uint8_t *rx_buffer, uint16_t max_len);

/**
 * @brief 注册接收回调
 * @param handle 句柄
 * @param cb 回调函数指针
 * @param user_ctx 用户自定义上下文指针，会在回调中传回
 */
void hal_uart_set_rx_cb(hal_uart_handle_t handle, hal_uart_rx_cb_t cb, void *user_ctx);

/* 临界区保护：商业级架构必备，用于解决并发冲突 */
void hal_enter_critical(void);

void hal_exit_critical(void);
#endif //HAL_UART_H
