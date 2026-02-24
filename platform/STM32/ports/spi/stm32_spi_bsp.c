#include "APP_config.h"
#if defined(CFG_TARGET_PLATFORM_STM32_HAL) && defined(CFG_FEAT_HAL_SPI) && (CFG_FEAT_HAL_SPI == 1)
#include <string.h>

#include "assert_cus.h"
#include "hal_spi.h"
#include "stm32_spi_bsp.h"
extern DMA_HandleTypeDef hdma_spi1_rx;
extern DMA_HandleTypeDef hdma_spi1_tx;
extern SPI_HandleTypeDef hspi1;
ret_code_t stm32_spi_bsp_get(uint8_t bus_id, stm32_spi_bsp_t *out) {
    ASSERT_PARAM(out != NULL);
    REQUIRE_RET(out != NULL, RET_MAKE_PARAM(RET_MOD_PORT, RET_SUB_PORT_SPI, RET_R_NULL_PTR));
    memset(out, 0, sizeof(*out));

    switch (bus_id) {
        case HAL_SPI_BUS1:
            out->hdma_rx      = &hdma_spi1_rx;
            out->hdma_tx      = &hdma_spi1_tx;
            out->hspi         = &hspi1;
            out->irq_prio     = 5;
            out->irq_sub_prio = 0;
            out->spi_irq      = SPI1_IRQn;
            break;
        default:
            return RET_MAKE_STATE(RET_MOD_PORT, RET_SUB_PORT_SPI, RET_R_NOT_READY);
    }
    return RET_OK;
}

uint32_t stm32_spi_busclk_hz(const SPI_HandleTypeDef *hspi) {
    ASSERT_PARAM(hspi != NULL);
    REQUIRE_RET(hspi != NULL, 48000000u);
    if (hspi->Instance == SPI1) {
        return 84000000u;
    }
    return 48000000u;
}

#endif
