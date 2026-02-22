#ifndef SMARTLOCK_STM32_HAL_CONFIG_H
#define SMARTLOCK_STM32_HAL_CONFIG_H

#include "platform_config.h"

/* ============================================================================
 * L3 平台层配置
 * 负责平台端口细节参数：DMA/Cache/中断策略等。
 * ============================================================================
 */

/* HAL 端口选项 */
#define CFG_PARAM_UART_RX_USE_DMA_IDLE    1 /* 启用 DMA + IDLE 接收 */
#define CFG_PARAM_UART_DISABLE_DMA_IT_HT  0 /* 是否关闭 DMA 半传输中断 */

/* STM32 H7 缓存选项（按平台能力配置） */
#define CFG_PARAM_STM32_DMA_CACHE_CLEAN       0
#define CFG_PARAM_STM32_DMA_CACHE_INVALIDATE  0

/* 参数合法性检查 */
#if ((CFG_PARAM_UART_RX_USE_DMA_IDLE != 0) && (CFG_PARAM_UART_RX_USE_DMA_IDLE != 1))
#error "CFG_PARAM_UART_RX_USE_DMA_IDLE 必须为 0 或 1。"
#endif
#if ((CFG_PARAM_UART_DISABLE_DMA_IT_HT != 0) && (CFG_PARAM_UART_DISABLE_DMA_IT_HT != 1))
#error "CFG_PARAM_UART_DISABLE_DMA_IT_HT 必须为 0 或 1。"
#endif
#if ((CFG_PARAM_STM32_DMA_CACHE_CLEAN != 0) && (CFG_PARAM_STM32_DMA_CACHE_CLEAN != 1))
#error "CFG_PARAM_STM32_DMA_CACHE_CLEAN 必须为 0 或 1。"
#endif
#if ((CFG_PARAM_STM32_DMA_CACHE_INVALIDATE != 0) && (CFG_PARAM_STM32_DMA_CACHE_INVALIDATE != 1))
#error "CFG_PARAM_STM32_DMA_CACHE_INVALIDATE 必须为 0 或 1。"
#endif

#endif /* SMARTLOCK_STM32_HAL_CONFIG_H */
