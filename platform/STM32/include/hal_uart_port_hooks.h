#ifndef HAL_UART_PORT_HOOKS_H
#define HAL_UART_PORT_HOOKS_H
#include "APP_config.h"
#include "stm32_hal_config.h"
/* hal抽象选择宏 */
#if defined(USE_STM32_HAL) && defined(ENABLE_HAL_UART)
#include "hal_uart.h"
#include "stm32_hal.h"

/*
 * 集成钩子（给上层/业务层事件回调用）：
 * 这些函数由 `platform/STM32/ports/hal_uart_port.c` 提供实现。
 * 你需要在 STM32 HAL 的 ISR/回调函数里调用它们，这样 `hal_uart_port.c`
 * 才能：
 *  - 提交 DMA RX 增量到软件 RingBuffer
 *  - 通过 `emit_evt()` 触发已注册的 `hal_uart_evt_cb_t`
 */

/* 放到 `HAL_UART_TxCpltCallback()` 中调用：通知 HAL_UART_EVT_TX_DONE */
void hal_uart_txCp_case(const UART_HandleTypeDef* huart);

/* 放到 `HAL_UART_ErrorCallback()` 中调用：通知 HAL_UART_EVT_ERROR */
void hal_uart_error_case(const UART_HandleTypeDef* huart);

/* 放到 `HAL_UARTEx_RxEventCallback()` 中调用（启用 `USE_HAL_UARTEx_ReceiveToIdle_DMA` 时）：提交 RX
 * 增量并通知 HAL_UART_EVT_RX */
void hal_uart_rx_event_case(const UART_HandleTypeDef* huart, uint16_t Size);

/* 可选：如果你希望用统一入口，在 `USARTx_IRQHandler` / DMA IRQHandler 里调用这些函数 */
void stm32_uart_irq_usart(hal_uart_id_t id);
void stm32_uart_irq_dma_rx(hal_uart_id_t id);
void stm32_uart_irq_dma_tx(hal_uart_id_t id);

#endif

#endif
