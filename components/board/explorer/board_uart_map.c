#include "board_uart_map.h"

/* 根据当前所属模块id 与返回状态生成32位状态码 */
#define UART_MAP_RET(errno_) RET_MAKE(RET_MOD_PORT, (errno_))

/* cubemx 或者自己初始化定义的句柄 */
/* 串口1 */
extern UART_HandleTypeDef huart1;
extern DMA_HandleTypeDef hdma_usart1_rx;
extern DMA_HandleTypeDef hdma_usart1_tx;

/* DMA 环形缓冲 + 软件 RB（静态） */
static uint8_t g_uart1_rx_dma[512];

ret_code_t stm32_uart_bsp_get(hal_uart_id_t id, hal_uart_cfg_t **cfg, stm32_uart_bsp_t *out) {
    if (!out) return -1;

    switch (id) {
        case HAL_UART_ID_0:
            /* 串口参数配置 */
            if (cfg && out->huart) {
                out->huart->Init.BaudRate = (*cfg)->baud;
                out->huart->Init.HwFlowCtl =
                    (*cfg)->flow_ctrl ? UART_HWCONTROL_RTS_CTS : UART_HWCONTROL_NONE;
                out->huart->Init.Parity = (*cfg)->parity;
                out->huart->Init.StopBits =
                    (*cfg)->stop_bits == STOPBITS_1 ? UART_STOPBITS_1 : UART_STOPBITS_2;
                out->huart->Init.WordLength =
                    (*cfg)->data_bits == WORDLENGTH_8B ? UART_WORDLENGTH_8B : UART_WORDLENGTH_9B;
            } else
                return UART_MAP_RET(RET_ERRNO_INVALID_ARG);
            /* 串口 MSP 配置 */
            out->huart      = &huart1;
            out->hdma_rx    = &hdma_usart1_rx;
            out->hdma_tx    = &hdma_usart1_tx;
            out->usart_irq  = USART1_IRQn;
            out->dma_rx_irq = DMA2_Stream2_IRQn;
            out->dma_tx_irq = DMA2_Stream7_IRQn;
            out->rx_dma_buf = g_uart1_rx_dma;          // DMA 内存侧地址
            out->rx_dma_len = sizeof(g_uart1_rx_dma);  // 长度 必须为2的幂次大小
            out->sw_rb_len  = 2048;                    /* 软件RB的容量 KB 尽量为2的幂次大小 */
            out->irq_prio   = 5;
            return UART_MAP_RET(RET_ERRNO_OK);
        default:
            return -1;
    }
}
