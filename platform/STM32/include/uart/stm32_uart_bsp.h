#ifndef STM32_UART_BSP_H
#define STM32_UART_BSP_H

#include "APP_config.h"
#include "stm32_hal_config.h"

#if (defined(CFG_TARGET_PLATFORM_STM32_HAL) && (CFG_TARGET_PLATFORM_STM32_HAL == 1)) && \
    (defined(CFG_FEAT_HAL_UART) && (CFG_FEAT_HAL_UART == 1))

#include <stdint.h>

#include "hal_uart.h"
#include "stm32_hal.h"

/**
 * @brief BSP 提供每路 UART 的硬件绑定和 HAL 接收缓冲建议值
 */
typedef struct {
    UART_HandleTypeDef* huart;
    DMA_HandleTypeDef* hdma_rx;
    DMA_HandleTypeDef* hdma_tx;
    IRQn_Type usart_irq;
    IRQn_Type dma_rx_irq;
    IRQn_Type dma_tx_irq;
    uint8_t* rx_dma_buf;
    uint32_t rx_dma_len;
    uint32_t hal_rx_buffer_len;
    uint32_t irq_prio;
    uint32_t irq_sub_prio;
} stm32_uart_bsp_t;

ret_code_t stm32_uart_bsp_get(hal_uart_id_t id, stm32_uart_bsp_t* out);

#endif

#endif
