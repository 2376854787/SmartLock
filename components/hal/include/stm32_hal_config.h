#ifndef HAL_CONFIG_H
#define HAL_CONFIG_H
/* 当前芯片平台选择宏开关 */
#define STM32F4XX /* STM32F4系列 */

/* 用于开启 stm32_port 的DMA 实现函数 */
#define USE_HAL_UARTEx_ReceiveToIdle_DMA       /* 启用 DMA + IDLE接收方式 */
#define DISABLE_DMA_IT_HT                false /* 是否关闭 DMA 过半中断  */

/* STM32 H7系列缓存优化 */
// #define SCB_CleanDCache_by_Addr
// #define SCB_InvalidateDCache_by_Addr
#endif  // HAL_CONFIG_H
