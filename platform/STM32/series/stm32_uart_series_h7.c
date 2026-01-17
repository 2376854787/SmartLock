#include "stm32_hal.h"
#include "stm32_uart_series.h"
#include <stdint.h>
#include "ret_code.h"
#include "utils_def.h"
#include "stm32_hal_config.h"

/* 默认 STM32H7 带 DCache：DMA 缓冲 32-byte 对齐 */
void stm32_uart_dma_tx_clean(const void *ptr, uint32_t len) {
#if defined(SCB_CleanDCache_by_Addr)
    if (!ptr || len == 0u) return;
    const uintptr_t start = (uintptr_t)ALIGN_DOWN((uintptr_t)ptr, 32u);
    const uintptr_t end   = (uintptr_t)ALIGN_UP((uintptr_t)ptr + (uintptr_t)len, 32u);
    SCB_CleanDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
#else
    (void)ptr;
    (void)len;
#endif
}

void stm32_uart_dma_rx_invalidate(const void *ptr, uint32_t len) {
#if defined(SCB_InvalidateDCache_by_Addr)
    if (!ptr || len == 0u) return;
    const uintptr_t start = (uintptr_t)ALIGN_DOWN((uintptr_t)ptr, 32u);
    const uintptr_t end   = (uintptr_t)ALIGN_UP((uintptr_t)ptr + (uintptr_t)len, 32u);
    SCB_InvalidateDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
#else
    (void)ptr;
    (void)len;
#endif
}
