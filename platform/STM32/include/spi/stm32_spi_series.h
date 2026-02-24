#ifndef STM32_SPI_SERIES_H
#define STM32_SPI_SERIES_H

#include "APP_config.h"
#include "stm32_hal_config.h"

#if defined(CFG_TARGET_PLATFORM_STM32_HAL) && (CFG_TARGET_PLATFORM_STM32_HAL == 1) && \
    defined(CFG_FEAT_HAL_SPI) && (CFG_FEAT_HAL_SPI == 1)
#include <stdint.h>

/* 系列差异：DMA + DCache 一致性维护。默认空实现，H7/F7 可覆盖。 */
void stm32_spi_dma_tx_clean(const void *ptr, uint32_t len);
void stm32_spi_dma_rx_invalidate(const void *ptr, uint32_t len);

#endif

#endif
