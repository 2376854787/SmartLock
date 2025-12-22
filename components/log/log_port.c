#include <stdio.h>

#include "log.h"
#include "usart.h"
#include "stm32f4xx_hal.h"
#include "ret_code.h"
// 简单忙标志：1=DMA/IT 正在发送
static volatile uint8_t s_uart_tx_busy = 0;

static int Log_uart_send_async(const uint8_t *d, uint16_t n, void *user)
{
    UART_HandleTypeDef *huart = (UART_HandleTypeDef *)user;
    if (!huart || !d || n == 0) return RET_E_INVALID_ARG;

    if (s_uart_tx_busy) return RET_E_BUSY;

    s_uart_tx_busy = 1;

    HAL_StatusTypeDef st = HAL_UART_Transmit_DMA(huart, (uint8_t *)d, n);
    if (st == HAL_OK) {
        return RET_OK; // 已成功启动DMA
    }

    // 启动失败：必须清busy，否则永远卡住
    s_uart_tx_busy = 0;

    // HAL_BUSY：外设忙（可能上一笔还没完全释放/状态机没回到READY）
    if (st == HAL_BUSY) return RET_E_BUSY;

    // 其它：真正错误
    return RET_E_IO;
}

/**
 * @brief 初始化LOG发送端配置
 */
void Log_PortInit(void) {
    Log_SetBackend((log_backend_t){.send_async = Log_uart_send_async, .user = &huart1});
}

/**
 * @brief 放在串口DMA发送完成回调中
 * @param huart 串口句柄
 */
void LOG_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart == &huart1) {
        s_uart_tx_busy = 0;
        Log_OnTxDoneISR(); /* 通知 LogTask：这笔发送结束 */
    }
}
