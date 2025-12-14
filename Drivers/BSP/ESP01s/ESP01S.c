#include "ESP01S.h"

#include <stdio.h>
#include <string.h>

#include "cmsis_os2.h"
#include "log.h"
#include "main.h"
#include "MemoryAllocation.h"
#include "RingBuffer.h"
#include "../../../components/AT/AT_Core_Task.h"

osMutexId_t esp01s_Mutex01Handle;
ESP01S_Handle g_esp01_handle;
volatile uint8_t g_esp01s_flag;

/**
 * @brief 初始化 ESP01s，使用 UART3 DMA + 空闲中断
 */
void esp01s_Init(UART_HandleTypeDef *huart, uint16_t rb_size) {
    /* 1、初始化 ESP01s的句柄 */
    g_esp01_handle.rb.name = "ESP01s_handle.rb";
    g_esp01_handle.uart_p = huart;
    g_esp01_handle.rb_rx_size = rb_size;
    g_esp01_handle.rx_len = 0;

    /* 2、初始化句柄中的环形缓冲区 */
    if (!CreateRingBuffer(&g_esp01_handle.rb, g_esp01_handle.rb_rx_size)) {
        printf("esp01s_Init() 初始化缓冲区失败\n");
    } else {
        printf("esp01s_Init() 初始化缓冲区成功\n");
    }
    LOG_W("heap", "%uKB- %u空间还剩余 %u", MEMORY_POND_MAX_SIZE, g_esp01_handle.rb_rx_size, query_remain_size());

    /* 4、发送命令 */
    /* AT命令 测试连通性 */
    //printf("返回值为%d\r\n", AT_SendCmd(&g_at_manager, "AT\r\n", "OK", 5000));
    if (AT_SendCmd(&g_at_manager, "AT\r\n", "OK", 5000) == AT_RESP_OK) {
        LOG_E("ESP01S", "AT 响应成功");
    } else {
        printf("%s  响应失败\n", "AT");
    }


    /* 开启station 模式 */
    // printf("返回值为%d\r\n", AT_SendCmd(&g_at_manager, "AT+CWMODE=1\r\n", "OK", 5000));
    if (AT_SendCmd(&g_at_manager, "AT+CWMODE=1\r\n", "OK", 5000) == AT_RESP_OK) {
        LOG_E("ESP01S", "AT 响应成功");
    } else {
        printf("%s  响应失败\n", "AT+CWMODE=1");
    }

    if (AT_SendCmd(&g_at_manager, "AT+CWSTOPSMART\r\n", "OK", 5000) == AT_RESP_OK) {
        LOG_E("ESP01S", "AT 响应成功");
    } else {
        printf("%s  响应失败\n", "AT+CWSTOPSMART");
    }

    /* 开启SmartConfig */
    //printf("返回值为%d\r\n", AT_SendCmd(&g_at_manager, "AT+CWSTARTSMART=3\r\n", "OK", 5000));
    if (AT_SendCmd(&g_at_manager, "AT+CWSTARTSMART=3\r\n", "CONNECTED", 20000) == AT_RESP_OK) {
        LOG_E("ESP01S", "AT 响应成功");
    } else {
        printf("%s  响应失败\n", "AT+CWSTARTSMART=3");
    }

    /* 关闭SmartConfig 模式 */
    //  printf("返回值为%d\r\n", AT_SendCmd(&g_at_manager, "AT+CWSTOPSMART\r\n", "OK", 5000));
}
