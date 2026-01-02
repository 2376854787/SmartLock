#include "ESP01S.h"

#include <stdio.h>
#include "log.h"
#include "MemoryAllocation.h"
#include "AT_Core_Task.h"

/**
 * @brief 初始化 ESP01s，使用 UART3 DMA + 空闲中断
 */
void esp01s_Init(UART_HandleTypeDef *huart, uint16_t rb_size) {
    (void)huart;
    (void)rb_size;

    /* 4、发送命令 */
    if (AT_SendCmd(&g_at_manager, "ATE0\r\n", "OK", 5000) == AT_RESP_OK) {
        LOG_E("ESP01S", "回显已关闭");
    } else { LOG_E("ESP01S", "%s  响应失败\n", "ATE0"); }

    if (AT_SendCmd(&g_at_manager, "AT\r\n", "OK", 5000) == AT_RESP_OK) {
        LOG_E("ESP01S", "AT 响应成功");
    } else {
        LOG_E("ESP01S", "%s  响应失败\n", "AT");
    }

    /* 网络联通测试 */
    while (AT_SendCmd(&g_at_manager, "AT+PING=\"www.baidu.com\"\r\n", "+PING:", 5000) != AT_RESP_OK) {
        LOG_E("ESP01S", "网络联通检查失败 将重新进行WiFi连接");
        /* 检查是否连接了wifi */
        if (AT_SendCmd(&g_at_manager, "AT+CWSTATE?\r\n", "0", 5000) == AT_RESP_OK) {
            LOG_E("ESP01S", "未连接至WiFi");
        }
        if (AT_SendCmd(&g_at_manager, "AT+CWSTATE?\r\n", "1", 5000) == AT_RESP_OK) {
            LOG_E("ESP01S", "已经连接上 AP，但尚未获取到 IPv4 地址");
        }
        if (AT_SendCmd(&g_at_manager, "AT+CWSTATE?\r\n", "2", 5000) == AT_RESP_OK) {
            LOG_E("ESP01S", "已经连接上 AP，已获取到 IPv4 地址");
        }
        if (AT_SendCmd(&g_at_manager, "AT+CWSTATE?\r\n", "3", 5000) == AT_RESP_OK) {
            LOG_E("ESP01S", "正在进行 Wi-Fi 连接或 Wi-Fi 重连");
        }
        LOG_W("ESP01S", "将重新进行wifi连接");
        /* 开启station 模式 */
        AT_SendCmd(&g_at_manager, "AT+CWMODE=1\r\n", "OK", 5000);
        /* 关闭SmartConfig */
        AT_SendCmd(&g_at_manager, "AT+CWSTOPSMART\r\n", "OK", 5000);
        /* 开启SmartConfig */
        AT_SendCmd(&g_at_manager, "AT+CWSTARTSMART=3\r\n", "CONNECTED", 60000);
    }
    LOG_E("ESP01S", "网络联通测试成功");
    /* 关闭SmartConfig */
    AT_SendCmd(&g_at_manager, "AT+CWSTOPSMART\r\n", "OK", 5000);
}
