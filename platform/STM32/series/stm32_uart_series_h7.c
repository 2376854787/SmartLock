#include "stm32_hal.h"
#include "stm32_uart_series.h"

/* 默认 STM32H7 带 DCache：DMA 缓冲建议 32-byte 对齐 */
void stm32_uart_dma_tx_clean(const void *ptr, uint32_t len) {
#if defined(SCB_CleanDCache_by_Addr)
    SCB_CleanDCache_by_Addr((uint32_t *)ptr, (int32_t)len);
#else
    (void)ptr;
    (void)len;
#endif
}

void stm32_uart_dma_rx_invalidate(const void *ptr, uint32_t len) {
#if defined(SCB_InvalidateDCache_by_Addr)
    SCB_InvalidateDCache_by_Addr((uint32_t *)ptr, (int32_t)len);
#else
    (void)ptr;
    (void)len;
#endif
}
