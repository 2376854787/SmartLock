#include "APP_config.h"

#if defined(CFG_TARGET_PLATFORM_STM32_HAL) && defined(CFG_FEAT_HAL_I2C) && (CFG_FEAT_HAL_I2C == 1)
#include <string.h>

#include "assert_cus.h"
#include "hal_i2c.h"
#include "stm32_i2c_bsp.h"

extern I2C_HandleTypeDef hi2c1;
extern DMA_HandleTypeDef hdma_i2c1_rx;
extern DMA_HandleTypeDef hdma_i2c1_tx;

ret_code_t stm32_i2c_bsp_get(uint8_t bus_id, stm32_i2c_bsp_t *out) {
    ASSERT_PARAM(out != NULL);
    REQUIRE_RET(out != NULL, RET_MAKE_PARAM(RET_MOD_PORT, RET_SUB_PORT_I2C, RET_R_NULL_PTR));
    memset(out, 0, sizeof(*out));

    switch (bus_id) {
        case HAL_I2C_BUS1:
            out->hi2c         = &hi2c1;
            out->hdma_rx      = &hdma_i2c1_rx;
            out->hdma_tx      = &hdma_i2c1_tx;
            out->i2c_ev_irq   = I2C1_EV_IRQn;
            out->i2c_er_irq   = I2C1_ER_IRQn;
            out->irq_prio     = 5u;
            out->irq_sub_prio = 0u;
            break;
        default:
            return RET_MAKE_STATE(RET_MOD_PORT, RET_SUB_PORT_I2C, RET_R_NOT_READY);
    }
    return RET_OK;
}

#endif
