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

typedef enum {
    ESP01S_WIFI_STATE_NOT_CONNECTED = 0,
    ESP01S_WIFI_STATE_CONNECTED_NO_IP,
    ESP01S_WIFI_STATE_CONNECTED_WITH_IP,
    ESP01S_WIFI_STATE_CONNECTING,
    ESP01S_WIFI_STATE_DISCONNECTED,
    ESP01S_WIFI_STATE_UNKNOWN = 0xFF
} esp01s_wifi_state_t;

static bool esp01s_ping_ok(void) {
    return (AT_SendCmd(&g_at_manager, "AT+PING=\"www.baidu.com\"\r\n", "OK",
                       ESP01S_CMD_TIMEOUT_MS) == AT_RESP_OK);
}

static esp01s_wifi_state_t esp01s_query_wifi_state(void) {
    static const struct {
        const char* prefix;
        esp01s_wifi_state_t state;
    } kStateMap[] = {
        {"+CWSTATE:0", ESP01S_WIFI_STATE_NOT_CONNECTED},
        {"+CWSTATE:1", ESP01S_WIFI_STATE_CONNECTED_NO_IP},
        {"+CWSTATE:2", ESP01S_WIFI_STATE_CONNECTED_WITH_IP},
        {"+CWSTATE:3", ESP01S_WIFI_STATE_CONNECTING},
        {"+CWSTATE:4", ESP01S_WIFI_STATE_DISCONNECTED},
    };

    for (uint32_t i = 0; i < (sizeof(kStateMap) / sizeof(kStateMap[0])); ++i) {
        if (AT_SendCmd(&g_at_manager, "AT+CWSTATE?\r\n", kStateMap[i].prefix,
                       ESP01S_CMD_TIMEOUT_MS) == AT_RESP_OK) {
            return kStateMap[i].state;
        }
    }

    return ESP01S_WIFI_STATE_UNKNOWN;
}

static void esp01s_report_wifi_state(esp01s_wifi_state_t state) {
    switch (state) {
        case ESP01S_WIFI_STATE_NOT_CONNECTED:
            LOG_E("ESP01S", "当前Wi-Fi状态: 未连接至 AP");
            break;
        case ESP01S_WIFI_STATE_CONNECTED_NO_IP:
            LOG_E("ESP01S", "当前Wi-Fi状态: 已连接 AP，但尚未获取到 IPv4 地址");
            break;
        case ESP01S_WIFI_STATE_CONNECTED_WITH_IP:
            LOG_E("ESP01S", "当前Wi-Fi状态: 已连接 AP，且已获取到 IPv4 地址");
            break;
        case ESP01S_WIFI_STATE_CONNECTING:
            LOG_E("ESP01S", "当前Wi-Fi状态: 正在进行 Wi-Fi 连接或 Wi-Fi 重连");
            break;
        case ESP01S_WIFI_STATE_DISCONNECTED:
            LOG_E("ESP01S", "当前Wi-Fi状态: Wi-Fi 已断开，但保留了目标 AP 信息");
            break;
        default:
            LOG_E("ESP01S", "当前Wi-Fi状态: 未知，AT+CWSTATE? 未匹配到已知状态");
            break;
    }
}

static bool esp01s_should_start_smartconfig(esp01s_wifi_state_t state) {
    return (state == ESP01S_WIFI_STATE_NOT_CONNECTED) ||
           (state == ESP01S_WIFI_STATE_CONNECTING) ||
           (state == ESP01S_WIFI_STATE_DISCONNECTED) ||
           (state == ESP01S_WIFI_STATE_UNKNOWN);
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
    /* AT测试 */
    if (AT_SendCmd(&g_at_manager, "ATE0\r\n", "OK", ESP01S_CMD_TIMEOUT_MS) == AT_RESP_OK) {
        LOG_E("ESP01S", "回显已关闭");
    } else { LOG_E("ESP01S", "%s  响应失败\n", "ATE0"); }

    /* 关闭回显 */
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
        const esp01s_wifi_state_t wifi_state = esp01s_query_wifi_state();

        LOG_E("ESP01S", "网络联通检查失败，将进行WiFi恢复 (attempt=%lu/%lu)",
              (unsigned long)attempt, (unsigned long)ESP01S_MAX_RECOVERY_ATTEMPTS);
        /* 检查是否连接了wifi */
        esp01s_report_wifi_state(wifi_state);

        if (esp01s_should_start_smartconfig(wifi_state)) {
            LOG_W("ESP01S", "当前状态适合重新配网，将进入 AirKiss/SmartConfig");
            /* 开启station 模式 */
            AT_SendCmd(&g_at_manager, "AT+CWMODE=1\r\n", "OK", ESP01S_CMD_TIMEOUT_MS);
            /* 关闭SmartConfig */
            AT_SendCmd(&g_at_manager, "AT+CWSTOPSMART\r\n", "OK", ESP01S_CMD_TIMEOUT_MS);
            /* 开启SmartConfig */
            (void)AT_SendCmd(&g_at_manager, "AT+CWSTARTSMART=3\r\n", "CONNECTED",
                             ESP01S_SMARTCONFIG_TIMEOUT_MS);
        } else {
            LOG_W("ESP01S", "Wi-Fi 已经关联到 AP，本次不进入 AirKiss，等待网络恢复");
        }

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
