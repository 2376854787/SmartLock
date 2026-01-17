#pragma once
#include <stdint.h>

/* 系列差异：Cache 一致性。默认空实现，H7/F7 覆盖 */
void stm32_uart_dma_tx_clean(const void *ptr, uint32_t len);
void stm32_uart_dma_rx_invalidate(const void *ptr, uint32_t len);
