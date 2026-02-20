#ifndef HAL_CONFIG_H
#define HAL_CONFIG_H

#include "platform_config.h"

/* HAL 端口选项 */
#define USE_HAL_UARTEx_ReceiveToIdle_DMA       /* 启用 DMA + IDLE 接收 */
#define DISABLE_DMA_IT_HT                false /* 是否关闭 DMA 半传输中断 */

/* STM32 H7 缓存选项 */
// #define SCB_CleanDCache_by_Addr
// #define SCB_InvalidateDCache_by_Addr

#endif  // HAL_CONFIG_H
