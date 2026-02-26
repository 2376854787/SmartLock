#ifndef STM32_I2C_SERIES_H
#define STM32_I2C_SERIES_H

#include "APP_config.h"
#include "stm32_hal_config.h"

#if defined(CFG_TARGET_PLATFORM_STM32_HAL) && defined(CFG_FEAT_HAL_I2C) && (CFG_FEAT_HAL_I2C == 1)
#include <stdint.h>

/* 系列差异：DMA + DCache 一致性维护。默认空实现，H7/F7 可覆盖。 */
void stm32_i2c_dma_tx_clean(const void *ptr, uint32_t len);
void stm32_i2c_dma_rx_invalidate(const void *ptr, uint32_t len);

#endif

#endif /* STM32_I2C_SERIES_H */
