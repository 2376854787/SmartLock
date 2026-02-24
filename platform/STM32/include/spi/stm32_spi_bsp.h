#ifndef STM32_SPI_BSP_H
#define STM32_SPI_BSP_H

#include "APP_config.h"
#include "stm32_hal_config.h"

#if defined(CFG_TARGET_PLATFORM_STM32_HAL) && defined(CFG_FEAT_HAL_SPI) && (CFG_FEAT_HAL_SPI == 1)
#include <stdint.h>
#include "ret_code.h"
#include "stm32_hal.h"

/* BSP：把 bus_id 绑定到真实硬件句柄/中断/DMA */
typedef struct {
    SPI_HandleTypeDef *hspi;    /* 必须有效 */
    DMA_HandleTypeDef *hdma_tx; /* 可为 NULL */
    DMA_HandleTypeDef *hdma_rx; /* 可为 NULL */
    IRQn_Type spi_irq;          /* 可选 */
    uint32_t irq_prio;
    uint32_t irq_sub_prio;
} stm32_spi_bsp_t;

ret_code_t stm32_spi_bsp_get(uint8_t bus_id, stm32_spi_bsp_t *out);


__WEAK uint32_t stm32_spi_busclk_hz(const SPI_HandleTypeDef *hspi);

#endif
#endif