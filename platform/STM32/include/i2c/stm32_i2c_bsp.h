#ifndef STM32_I2C_BSP_H
#define STM32_I2C_BSP_H

#include "APP_config.h"
#include "stm32_hal_config.h"

#if defined(CFG_TARGET_PLATFORM_STM32_HAL) && defined(CFG_FEAT_HAL_I2C) && (CFG_FEAT_HAL_I2C == 1)
#include <stdint.h>

#include "ret_code_t.h"
#include "stm32_hal.h"

typedef struct {
    I2C_HandleTypeDef *hi2c;    /* 必须有效 */
    DMA_HandleTypeDef *hdma_tx; /* 可为 NULL */
    DMA_HandleTypeDef *hdma_rx; /* 可为 NULL */
    IRQn_Type i2c_ev_irq;       /* 可选 */
    IRQn_Type i2c_er_irq;       /* 可选 */
    uint32_t irq_prio;
    uint32_t irq_sub_prio;
} stm32_i2c_bsp_t;

ret_code_t stm32_i2c_bsp_get(uint8_t bus_id, stm32_i2c_bsp_t *out);

#endif

#endif /* STM32_I2C_BSP_H */
