#ifndef STM32_UART_BSP_H
#define STM32_UART_BSP_H
#include "APP_config.h"
#include "stm32_hal_config.h"
#if defined(USE_STM32_HAL) && defined(ENABLE_HAL_UART)
#include <stdint.h>

#include "hal_uart.h"
#include "stm32_hal.h"

/* BSP 提供每路 UART 的绑定资源（实例/DMA/缓存/优先级） */
typedef struct {
    UART_HandleTypeDef* huart;   // HAL UART 句柄（需已配置 Instance）
    DMA_HandleTypeDef* hdma_rx;  // 可为 NULL（不启用 DMA RX）
    DMA_HandleTypeDef* hdma_tx;  // 可为 NULL（不启用 DMA TX）
    IRQn_Type usart_irq;         // USARTx_IRQn
    IRQn_Type dma_rx_irq;        // DMA RX IRQ（可选）
    IRQn_Type dma_tx_irq;        // DMA TX IRQ（可选）
    uint8_t* rx_dma_buf;         // DMA 环形缓冲
    uint32_t rx_dma_len;         // 环形缓冲长度 必须为2的幂 否则出错
    uint32_t sw_rb_len;          // 软件 RB 长度 默认1024
    uint32_t irq_prio;           // NVIC 优先级
} stm32_uart_bsp_t;

/* 板级实现 */
ret_code_t stm32_uart_bsp_get(hal_uart_id_t id, stm32_uart_bsp_t* out);

#endif

#endif