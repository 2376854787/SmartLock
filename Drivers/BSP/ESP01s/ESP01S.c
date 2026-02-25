#include "ESP01S.h"

#include "log.h"
#include "AT_Core_Task.h"

enum {
    ESP01S_CMD_TIMEOUT_MS            = 5000u,
    ESP01S_SMARTCONFIG_TIMEOUT_MS    = 60000u,
    ESP01S_MAX_RECOVERY_ATTEMPTS     = 4u,
    ESP01S_BACKOFF_BASE_MS           = 1000u,
    ESP01S_BACKOFF_MAX_MS            = 8000u,
};

static bool esp01s_ping_ok(void) {
    return (AT_SendCmd(&g_at_manager, "AT+PING=\"www.baidu.com\"\r\n", "OK",
                       ESP01S_CMD_TIMEOUT_MS) == AT_RESP_OK);
}

static void esp01s_report_wifi_state(void) {
    if (AT_SendCmd(&g_at_manager, "AT+CWSTATE?\r\n", "0", ESP01S_CMD_TIMEOUT_MS) == AT_RESP_OK) {
        LOG_E("ESP01S", "未连接至WiFi");
    }
    if (AT_SendCmd(&g_at_manager, "AT+CWSTATE?\r\n", "1", ESP01S_CMD_TIMEOUT_MS) == AT_RESP_OK) {
        LOG_E("ESP01S", "已经连接上 AP，但尚未获取到 IPv4 地址");
    }
    if (AT_SendCmd(&g_at_manager, "AT+CWSTATE?\r\n", "2", ESP01S_CMD_TIMEOUT_MS) == AT_RESP_OK) {
        LOG_E("ESP01S", "已经连接上 AP，已获取到 IPv4 地址");
    }
    if (AT_SendCmd(&g_at_manager, "AT+CWSTATE?\r\n", "3", ESP01S_CMD_TIMEOUT_MS) == AT_RESP_OK) {
        LOG_E("ESP01S", "正在进行 Wi-Fi 连接或 Wi-Fi 重连");
    }
}

static uint32_t esp01s_backoff_ms(uint32_t retry_idx) {
    uint32_t backoff = ESP01S_BACKOFF_BASE_MS;
    while (retry_idx > 0u && backoff < ESP01S_BACKOFF_MAX_MS) {
        backoff <<= 1u;
        retry_idx--;
    }
    if (backoff > ESP01S_BACKOFF_MAX_MS) backoff = ESP01S_BACKOFF_MAX_MS;
    return backoff;
}

/**
 * @brief 初始化 ESP01s，使用 UART3 DMA + 空闲中断
 */
void esp01s_Init(void) {
    /* 4、发送命令 */
    if (AT_SendCmd(&g_at_manager, "ATE0\r\n", "OK", ESP01S_CMD_TIMEOUT_MS) == AT_RESP_OK) {
        LOG_E("ESP01S", "回显已关闭");
    } else { LOG_E("ESP01S", "%s  响应失败\n", "ATE0"); }

    if (AT_SendCmd(&g_at_manager, "AT\r\n", "OK", ESP01S_CMD_TIMEOUT_MS) == AT_RESP_OK) {
        LOG_E("ESP01S", "AT 响应成功");
    } else {
        LOG_E("ESP01S", "%s  响应失败\n", "AT");
    }
    OSAL_delay_ms(ESP01S_CMD_TIMEOUT_MS);
    /* 网络联通测试 */
    if (esp01s_ping_ok()) {
        LOG_E("ESP01S", "网络联通测试成功");
        /* 关闭SmartConfig */
        AT_SendCmd(&g_at_manager, "AT+CWSTOPSMART\r\n", "OK", ESP01S_CMD_TIMEOUT_MS);
        return;
    }

    for (uint32_t attempt = 1; attempt <= ESP01S_MAX_RECOVERY_ATTEMPTS; attempt++) {
        LOG_E("ESP01S", "网络联通检查失败，将进行WiFi恢复 (attempt=%lu/%lu)",
              (unsigned long)attempt, (unsigned long)ESP01S_MAX_RECOVERY_ATTEMPTS);
        /* 检查是否连接了wifi */
        esp01s_report_wifi_state();
        LOG_W("ESP01S", "将重新进行wifi连接");
        /* 开启station 模式 */
        AT_SendCmd(&g_at_manager, "AT+CWMODE=1\r\n", "OK", ESP01S_CMD_TIMEOUT_MS);
        /* 关闭SmartConfig */
        AT_SendCmd(&g_at_manager, "AT+CWSTOPSMART\r\n", "OK", ESP01S_CMD_TIMEOUT_MS);
        /* 开启SmartConfig */
        (void)AT_SendCmd(&g_at_manager, "AT+CWSTARTSMART=3\r\n", "CONNECTED",
                         ESP01S_SMARTCONFIG_TIMEOUT_MS);

        if (esp01s_ping_ok()) {
            LOG_E("ESP01S", "网络联通测试成功");
            /* 关闭SmartConfig */
            AT_SendCmd(&g_at_manager, "AT+CWSTOPSMART\r\n", "OK", ESP01S_CMD_TIMEOUT_MS);
            return;
        }

        if (attempt < ESP01S_MAX_RECOVERY_ATTEMPTS) {
            const uint32_t backoff_ms = esp01s_backoff_ms(attempt - 1u);
            LOG_W("ESP01S", "网络仍不可达，%lu ms 后重试", (unsigned long)backoff_ms);
            (void)OSAL_delay_ms(backoff_ms);
        }
    }

    LOG_E("ESP01S", "网络恢复失败，已达到最大重试次数");
    /* 关闭SmartConfig */
    AT_SendCmd(&g_at_manager, "AT+CWSTOPSMART\r\n", "OK", ESP01S_CMD_TIMEOUT_MS);
}
