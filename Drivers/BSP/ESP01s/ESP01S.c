#include "ESP01S.h"

#include <stdio.h>
#include <string.h>

#include "cmsis_os2.h"
#include "main.h"
#include "RingBuffer.h"
#include "usart.h"

osMutexId_t esp01s_Mutex01Handle;
ESP01S_Handle g_esp01_handle;
volatile uint8_t g_esp01s_flag;

/**
 * @brief 初始化 ESP01s，使用 UART3 DMA + 空闲中断
 */
void esp01s_Init(UART_HandleTypeDef *huart, uint16_t rb_size) {
    g_esp01_handle.rb.name = "ESP01s_handle.rb";
    g_esp01_handle.uart_p = huart;
    g_esp01_handle.rb_rx_size = rb_size;
    g_esp01_handle.rx_len = 0;

    if (!CreateRingBuffer(&g_esp01_handle.rb, g_esp01_handle.rb_rx_size)) {
        printf("esp01s_Init() 初始化缓冲区失败\n");
    } else {
        printf("esp01s_Init() 初始化缓冲区成功\n");
    }

    HAL_UARTEx_ReceiveToIdle_DMA(g_esp01_handle.uart_p,
                                 g_esp01_handle.rx_buffer,
                                 sizeof(g_esp01_handle.rx_buffer));
    __HAL_DMA_DISABLE_IT(g_esp01_handle.uart_p->hdmarx, DMA_IT_HT);

    if (command_send(g_esp01_handle.uart_p, (const uint8_t *) "AT\r\n", "OK", 2000)) {
        printf("\"AT\\r\\n\" 响应成功\n");
    } else {
        printf("\"AT\\r\\n\" 响应失败\n");
    }
}

bool command_send(UART_HandleTypeDef *huart, const uint8_t *command, char *wait_rsu, uint16_t max_wait_time) {
    g_esp01s_flag = 0;
    g_esp01_handle.rx_len = 0;
    g_esp01_handle.rx_buffer[0] = '\0';

    HAL_UARTEx_ReceiveToIdle_DMA(huart, g_esp01_handle.rx_buffer, sizeof(g_esp01_handle.rx_buffer));
    __HAL_DMA_DISABLE_IT(huart->hdmarx, DMA_IT_HT);

    HAL_UART_Transmit(huart, command, strlen((char *) command), max_wait_time);

    while (max_wait_time--) {
        if (g_esp01s_flag) {
            if (strstr((char *) g_esp01_handle.rx_buffer, wait_rsu) != NULL) {
                return true;
            }
            g_esp01s_flag = 0;
        }
        osDelay(1);
    }
    return false;
}
