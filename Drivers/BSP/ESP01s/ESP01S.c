#include "ESP01S.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "main.h"
#include "usart.h"
#include "cmsis_os.h"   // osDelay

#define ESP_UART   (&huart3)

/* —— 接收累积区（供整个工程统一查看/匹配）——
 * 注意：USART3 的 DMA 实际写入的是 freertos.c 里的 esp3_dma_buf，
 * 由回调把数据“追加”到本缓冲区，不覆盖！
 */
char esp8266_rx_buffer[1024] = {0};
volatile uint16_t rx_len = 0;   // 记录当前已累计的字节数（可被中断更新）

/* 工具：打印最近追加的片段（调试用） */
static void dump_tail(const char* tag, uint16_t start)
{
    if (rx_len > start) {
        uint16_t n = rx_len - start;
        if (n > 120) n = 120;   // 避免过长
        printf("[ESP8266] %s tail(%uB): ", tag, (unsigned)n);
        for (uint16_t i = 0; i < n; i++) {
            char c = esp8266_rx_buffer[start + i];
            putchar((c >= 32 && c <= 126) ? c : '.');
        }
        putchar('\n');
    }
}

/**
 * @brief  发送 AT 指令并在“新增接收区间”内等待某个关键字
 * @param  cmd     要发送的命令（必须含 \r\n）
 * @param  ack     期望匹配到的关键字（可为 NULL，表示仅发送）
 * @param  timeout 超时（ms）
 * @return true 匹配到；false 超时
 */
bool bsp_esp8266_SendCommand(const char *cmd, const char *ack, uint32_t timeout)
{
    if (!cmd) return false;

    // 记录“发送前”的接收写指针，从这里开始才算“新数据”
    uint16_t start = rx_len;

    // 发送
    HAL_StatusTypeDef txok = HAL_UART_Transmit(ESP_UART, (uint8_t*)cmd, (uint16_t)strlen(cmd), 1000);
    if (txok != HAL_OK) {
        printf("[ESP8266] TX ERROR for: %s", cmd);
        return false;
    }
    printf("[ESP8266] TX: %s", cmd);

    if (ack == NULL) return true;

    // 轮询等待新增的数据中出现 ack
    uint32_t t0 = HAL_GetTick();
    while ((HAL_GetTick() - t0) < timeout) {
        // 仅在“新增片段”里查找（避免被历史残留干扰）
        if (start < rx_len) {
            char *p = strstr(&esp8266_rx_buffer[start], ack);
            if (p) {
                printf("[ESP8266] RX: (matched \"%s\")\n", ack);
                return true;
            }
        }
        osDelay(10);
    }

    // 超时打印最近片段，便于定位
    printf("[ESP8266] TIMEOUT! No response for: %s", cmd);
    dump_tail("Last RX", start);
    return false;
}
extern char esp8266_rx_buffer[1024];
extern volatile uint16_t rx_len;

// 可选：若未使用 FreeRTOS，可把这两个宏改为空壳
#ifndef taskENTER_CRITICAL
#define taskENTER_CRITICAL()  __disable_irq()
#define taskEXIT_CRITICAL()   __enable_irq()
#endif

static inline uint32_t ms_since(uint32_t t0) {
    return HAL_GetTick() - t0;
}

bool esp_join_ap(const char* ssid, const char* pass, uint32_t timeout_ms)
{
    if (!ssid || !pass) return false;

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, pass);

    // 记录“发送前”的接收写指针 —— 只在新增数据里匹配
    uint16_t start;
    taskENTER_CRITICAL();
    start = rx_len;
    taskEXIT_CRITICAL();

    // 仅发送，不在这里等 ack（因为 CWJAP 返回是多阶段）
    if (!bsp_esp8266_SendCommand(cmd, NULL, 1000)) {
        printf("[CWJAP] TX failed\n");
        return false;
    }

    uint32_t t0 = HAL_GetTick();
    for (;;) {
        if (ms_since(t0) > timeout_ms) {
            // 超时前打印最近一段，便于排错
            taskENTER_CRITICAL();
            uint16_t end = rx_len;
            taskEXIT_CRITICAL();
            if (end > start) {
                uint16_t n = end - start;
                if (n > 120) n = 120;
                printf("[CWJAP] TIMEOUT. tail(%uB): ", (unsigned)n);
                for (uint16_t i = 0; i < n; ++i) {
                    char c = esp8266_rx_buffer[end - n + i];
                    putchar((c >= 32 && c <= 126) ? c : '.');
                }
                putchar('\n');
            }
            return false;
        }

        // 快照当前已接收的尾指针
        uint16_t end;
        taskENTER_CRITICAL();
        end = rx_len;
        // 保障后续 strstr 安全：临时放一个 0 结尾（不会越界，因为 rx_len 始终在缓冲区内维护）
        if (end < sizeof(esp8266_rx_buffer)) {
            esp8266_rx_buffer[end] = '\0';
        } else {
            // 极端情况保护
            esp8266_rx_buffer[sizeof(esp8266_rx_buffer) - 1] = '\0';
            end = sizeof(esp8266_rx_buffer) - 1;
        }
        taskEXIT_CRITICAL();

        if (end > start) {
            // 仅在“新增片段”中匹配
            const char* seg = &esp8266_rx_buffer[start];

            if (strstr(seg, "WIFI CONNECTED")) {
                printf("[CWJAP] WIFI CONNECTED\n");
            }
            if (strstr(seg, "WIFI GOT IP")) {
                printf("[CWJAP] WIFI GOT IP\n");
            }
            if (strstr(seg, "FAIL") || strstr(seg, "ERROR")) {
                printf("[CWJAP] FAIL/ERROR\n");
                return false;
            }
            if (strstr(seg, "\r\nOK\r\n") || strstr(seg, "OK")) {
                printf("[CWJAP] OK\n");
                return true;
            }

            // 只向前推进到当前末尾，下一轮只查更“新的”数据，避免重复扫描
            start = end;
        }

        osDelay(20);
    }
}

/**
 * @brief  ESP8266 基础初始化（不重启 DMA！DMA 在 freertos.c 里统一启动）
 */
void bsp_esp8266_Init(void)
{
    // 上电稳定
    HAL_Delay(1000);

    // 1. 测试 AT
    while (!bsp_esp8266_SendCommand("AT\r\n", "OK", 1000)) {
        HAL_Delay(500);
    }

    // 2. 关闭回显
    (void)bsp_esp8266_SendCommand("ATE0\r\n", "OK", 1000);
    HAL_Delay(100);

    // 3. Station 模式
    (void)bsp_esp8266_SendCommand("AT+CWMODE=1\r\n", "OK", 1500);
    HAL_Delay(100);

    // 4. DHCP 开启（station, enable）
    (void)bsp_esp8266_SendCommand("AT+CWDHCP=1,1\r\n", "OK", 1500);
    HAL_Delay(100);

    // 5. 复位并等待 ready（复位期间会输出 WIFI DISCONNECT 等杂项，不当做失败）
   // (void)bsp_esp8266_SendCommand("AT+RST\r\n", "ready", 12000);
   // HAL_Delay(1500);

    printf("ESP8266_Init finished!\n");
}
