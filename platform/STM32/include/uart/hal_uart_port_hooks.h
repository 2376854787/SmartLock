#ifndef HAL_UART_PORT_HOOKS_H
#define HAL_UART_PORT_HOOKS_H

#include "APP_config.h"
#include "stm32_hal_config.h"

#if (defined(CFG_TARGET_PLATFORM_STM32_HAL) && (CFG_TARGET_PLATFORM_STM32_HAL == 1)) && \
    (defined(CFG_FEAT_HAL_UART) && (CFG_FEAT_HAL_UART == 1))

#include "hal_uart.h"
#include "stm32_hal.h"

/**
 * @brief STM32 UART port ISR/回调桥接入口
 * @note 这些函数只负责把硬件事件送入 UART HAL，不直接触发业务层逻辑
 */
void hal_uart_txCp_case(const UART_HandleTypeDef* huart);
void hal_uart_error_case(const UART_HandleTypeDef* huart);
void hal_uart_rx_event_case(const UART_HandleTypeDef* huart, uint16_t Size);
void hal_uart_rx_dma_progress_case(const UART_HandleTypeDef* huart);
void stm32_uart_irq_usart(hal_uart_id_t id);
void stm32_uart_irq_dma_rx(hal_uart_id_t id);
void stm32_uart_irq_dma_tx(hal_uart_id_t id);

#endif

#endif
