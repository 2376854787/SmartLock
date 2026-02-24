#include "APP_config.h"
#include "stm32_hal_config.h"
#if (defined(CFG_TARGET_PLATFORM_STM32_HAL) && (CFG_TARGET_PLATFORM_STM32_HAL == 1)) && (defined(CFG_FEAT_HAL_UART) && (CFG_FEAT_HAL_UART == 1))
#include <stdint.h>

#include "assert_cus.h"
#include "stm32_uart_series.h"

/* 默认 STM32H7 带 DCache：DMA 缓冲 32-byte 对齐 */
void stm32_uart_dma_tx_clean(const void* ptr, uint32_t len) {
#if (defined(CFG_PARAM_STM32_DMA_CACHE_CLEAN) && (CFG_PARAM_STM32_DMA_CACHE_CLEAN == 1))
    ASSERT_PARAM((ptr != NULL) || (len == 0u));
    REQUIRE_RET_VOID((ptr != NULL) || (len == 0u));
    if (len == 0u) return;
    const uintptr_t start = (uintptr_t)ALIGN_DOWN((uintptr_t)ptr, 32u);
    const uintptr_t end   = (uintptr_t)ALIGN_UP((uintptr_t)ptr + (uintptr_t)len, 32u);
    SCB_CleanDCache_by_Addr((uint32_t*)start, (int32_t)(end - start));
#else
    (void)ptr;
    (void)len;
#endif
}

void stm32_uart_dma_rx_invalidate(const void* ptr, uint32_t len) {
#if (defined(CFG_PARAM_STM32_DMA_CACHE_INVALIDATE) && (CFG_PARAM_STM32_DMA_CACHE_INVALIDATE == 1))
    ASSERT_PARAM((ptr != NULL) || (len == 0u));
    REQUIRE_RET_VOID((ptr != NULL) || (len == 0u));
    if (len == 0u) return;
    const uintptr_t start = (uintptr_t)ALIGN_DOWN((uintptr_t)ptr, 32u);
    const uintptr_t end   = (uintptr_t)ALIGN_UP((uintptr_t)ptr + (uintptr_t)len, 32u);
    SCB_InvalidateDCache_by_Addr((uint32_t*)start, (int32_t)(end - start));
#else
    (void)ptr;
    (void)len;
#endif
}

#endif


