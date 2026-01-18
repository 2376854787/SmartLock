#ifndef STM32F7H7_HAL_UART_BSP_H
#define STM32F7H7_HAL_UART_BSP_H
#include "APP_config.h"
#include "stm32_hal_config.h"
/* hal抽象选择宏 */
#if defined(USE_STM32_HAL) && defined(ENABLE_HAL_UART)
#include <stdint.h>

/* 系列差异：Cache 一致性。默认空实现，H7/F7 覆盖 */
void stm32_uart_dma_tx_clean(const void* ptr, uint32_t len);
void stm32_uart_dma_rx_invalidate(const void* ptr, uint32_t len);

#endif

#endif
