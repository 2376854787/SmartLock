#ifndef APP_AS608_PORT_H
#define APP_AS608_PORT_H

#include <stdint.h>
#include "main.h"   /* 依赖 UART_HandleTypeDef 等定义 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  绑定用于 AS608 的 UART（建议独占一个 UART）。
 * @note   在调用任何 AS608 API 之前先调用一次。
 */
void AS608_Port_BindUart(UART_HandleTypeDef *huart);

/**
 * @brief  启动 UART RX（中断模式，1 字节循环接收）。
 * @note   通常在 AS608 任务启动后调用一次。
 */
void AS608_Port_StartRx(void);

/**
 * @brief  将 HAL_UART_RxCpltCallback 转发到这里（避免你工程中多处定义回调冲突）。
 * @note   在你工程自己的 HAL_UART_RxCpltCallback 中调用：
 *         AS608_Port_OnUartRxCplt(huart);
 */
void AS608_Port_OnUartRxCplt(UART_HandleTypeDef *huart);

/**
 * @brief  将 HAL_UART_ErrorCallback 转发到这里（可选）。
 */
void AS608_Port_OnUartError(UART_HandleTypeDef *huart);

/**
 * @brief  清空 AS608 RX 缓冲。
 */
void AS608_Port_FlushRx(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_AS608_PORT_H */
