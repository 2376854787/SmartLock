// Huawei Cloud MQTT task over ESP8266 (AT commands)
#include "wifi_mqtt_task.h"

#include <stdbool.h>

#include "ESP01S.h"
#include "cmsis_os.h"
#include "usart.h"
#include "main.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern UART_HandleTypeDef huart3;
#define ESP_UART (&huart3)

// From ESP01S.c
extern char esp8266_rx_buffer[1024];
extern volatile uint16_t rx_len;

static void esp_clear_rx(void) {
    memset(esp8266_rx_buffer, 0, sizeof(esp8266_rx_buffer));
    rx_len = 0;
}

static bool esp_wait_text(const char *needle, uint32_t timeout_ms) {
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeout_ms) {
        if (rx_len > 0 && strstr(esp8266_rx_buffer, needle)) {
            return true;
        }
        osDelay(10);
    }
    return false;
}

static bool esp_wait_any_text2(const char *needle1, const char *needle2, uint32_t timeout_ms) {
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeout_ms) {
        if (rx_len > 0) {
            if ((needle1 && strstr(esp8266_rx_buffer, needle1)) ||
                (needle2 && strstr(esp8266_rx_buffer, needle2))) {
                return true;
            }
        }
        osDelay(10);
    }
    return false;
}

static bool esp_send_raw(const uint8_t *data, uint16_t len, uint32_t timeout_ms) {
    HAL_UART_Transmit(ESP_UART, (uint8_t *) data, len, HAL_MAX_DELAY);
    // Expect SEND OK from ESP8266
    return esp_wait_text("SEND OK", timeout_ms);
}

static bool esp_cipsend_and_send(const uint8_t *data, uint16_t len, uint32_t timeout_ms) {
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u\r\n", (unsigned) len);
    esp_clear_rx();
    HAL_UART_Transmit(ESP_UART, (uint8_t *) cmd, (uint16_t) strlen(cmd), HAL_MAX_DELAY);
    if (!esp_wait_text(">", 3000)) {
        printf("[MQTT] No prompt '>' for CIPSEND\r\n");
        return false;
    }
    return esp_send_raw(data, len, timeout_ms);
}

// MQTT helpers
static size_t mqtt_encode_varint(uint8_t *out, size_t value) {
    size_t idx = 0;
    do {
        uint8_t byte = value % 128;
        value /= 128;
        if (value > 0) byte |= 0x80;
        out[idx++] = byte;
    } while (value > 0);
    return idx;
}

static void put_utf8_str(uint8_t **p, const char *s) {
    size_t len = strlen(s);
    *(*p)++ = (uint8_t) ((len >> 8) & 0xFF);
    *(*p)++ = (uint8_t) (len & 0xFF);
    memcpy(*p, s, len);
    *p += len;
}

static size_t mqtt_build_connect(uint8_t *buf, size_t buflen,
                                 const char *client_id,
                                 const char *username,
                                 const char *password,
                                 uint16_t keepalive) {
    uint8_t *p = buf;
    uint8_t vh[256];
    uint8_t *q = vh;

    // Variable header
    put_utf8_str(&q, "MQTT");
    *q++ = 4; // protocol level 3.1.1
    uint8_t flags = 0;
    flags |= (1 << 1); // clean session
    if (username && username[0]) flags |= (1 << 7);
    if (password && password[0]) flags |= (1 << 6);
    *q++ = flags;
    *q++ = (uint8_t) (keepalive >> 8);
    *q++ = (uint8_t) (keepalive & 0xFF);
    size_t vh_len = (size_t) (q - vh);

    // Payload
    uint8_t pl[512];
    uint8_t *r = pl;
    put_utf8_str(&r, client_id);
    if (username && username[0]) put_utf8_str(&r, username);
    if (password && password[0]) put_utf8_str(&r, password);
    size_t pl_len = (size_t) (r - pl);

    size_t rem_len = vh_len + pl_len;
    // Fixed header
    *p++ = 0x10; // CONNECT
    p += mqtt_encode_varint(p, rem_len);
    // Copy VH + PL
    memcpy(p, vh, vh_len); p += vh_len;
    memcpy(p, pl, pl_len); p += pl_len;
    return (size_t) (p - buf);
}

static size_t mqtt_build_publish(uint8_t *buf, size_t buflen,
                                 const char *topic,
                                 const uint8_t *payload, size_t payload_len) {
    uint8_t *p = buf;
    uint8_t vh[256];
    uint8_t *q = vh;
    put_utf8_str(&q, topic);
    size_t vh_len = (size_t) (q - vh);
    size_t rem_len = vh_len + payload_len;
    *p++ = 0x30; // PUBLISH QoS0
    p += mqtt_encode_varint(p, rem_len);
    memcpy(p, vh, vh_len); p += vh_len;
    memcpy(p, payload, payload_len); p += payload_len;
    return (size_t) (p - buf);
}

static size_t mqtt_build_pingreq(uint8_t *buf, size_t buflen) {
    if (buflen < 2) return 0;
    buf[0] = 0xC0; buf[1] = 0x00; return 2;
}

static bool mqtt_wait_connack_ok(uint32_t timeout_ms) {
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeout_ms) {
        if (rx_len >= 4) {
            // Look for 0x20 0x02 xx 0x00
            for (uint16_t i = 0; i + 3 < rx_len; ++i) {
                uint8_t *b = (uint8_t *) &esp8266_rx_buffer[i];
                if (b[0] == 0x20 && b[1] == 0x02 && b[3] == 0x00) {
                    return true;
                }
            }
        }
        if (rx_len > 0 && strstr(esp8266_rx_buffer, "CLOSED")) return false;
        osDelay(20);
    }
    return false;
}

static bool mqtt_wait_pingresp(uint32_t timeout_ms) {
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeout_ms) {
        if (rx_len >= 2) {
            for (uint16_t i = 0; i + 1 < rx_len; ++i) {
                uint8_t *b = (uint8_t *) &esp8266_rx_buffer[i];
                if (b[0] == 0xD0 && b[1] == 0x00) return true;
            }
        }
        if (rx_len > 0 && strstr(esp8266_rx_buffer, "CLOSED")) return false;
        osDelay(20);
    }
    return false;
}

static bool mqtt_connect_sequence(void) {
    // Ensure ESP is inited (idempotent)
    bsp_esp8266_Init();

    // Join AP
    char cmd[128];
    if (!esp_join_ap(WIFI_SSID, WIFI_PASS, 20000)) {
        printf("[MQTT] Join AP failed\n");
        // 可重试或返回
    } else {
        printf("[MQTT] Join AP success\n");
    }

    // Single connection, normal mode
    bsp_esp8266_SendCommand("AT+CIPMUX=0\r\n", "OK", 2000);
    bsp_esp8266_SendCommand("AT+CIPMODE=0\r\n", "OK", 2000);

    // Start TCP/SSL
    const char *proto = (IOTDA_USE_SSL ? "SSL" : "TCP");
    if (IOTDA_USE_SSL) {
        // Optional: increase SSL buffer size for handshake
        bsp_esp8266_SendCommand("AT+CIPSSLSIZE=4096\r\n", "OK", 2000);
        // If your firmware supports certificate verify options,
        // configure them here per AT manual.
    }
    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"%s\",\"%s\",%d\r\n", proto, IOTDA_HOST, IOTDA_PORT);
    if (!bsp_esp8266_SendCommand(cmd, "OK", 15000)) {
        printf("[MQTT] CIPSTART no OK\r\n");
        return false;
    }
    // Wait CONNECT or ALREADY
    if (!esp_wait_any_text2("CONNECT", "ALREADY", 20000)) {
        printf("[MQTT] No CONNECT after CIPSTART\r\n");
        return false;
    }

    // Build CONNECT
    uint8_t pkt[512];
    size_t len = mqtt_build_connect(pkt, sizeof(pkt), IOTDA_CLIENT_ID, IOTDA_USERNAME, IOTDA_PASSWORD, IOTDA_KEEPALIVE_SECONDS);
    if (len == 0) return false;

    // Send CONNECT
    if (!esp_cipsend_and_send(pkt, (uint16_t) len, 8000)) {
        printf("[MQTT] Send CONNECT failed\r\n");
        return false;
    }

    // Wait CONNACK
    if (!mqtt_wait_connack_ok(8000)) {
        printf("[MQTT] No CONNACK OK\r\n");
        return false;
    }
    printf("[MQTT] Connected\r\n");
    return true;
}

static bool mqtt_publish_properties_tick(void) {
    char topic[160];
    snprintf(topic, sizeof(topic), "$oc/devices/%s/sys/properties/report", IOTDA_DEVICE_ID);
    char payload[192];
    // Simple payload: report tick
    snprintf(payload, sizeof(payload), "{\"services\":[{\"service_id\":\"default\",\"properties\":{\"tick\":%lu}}]}", HAL_GetTick());

    uint8_t pkt[512];
    size_t len = mqtt_build_publish(pkt, sizeof(pkt), topic, (const uint8_t *) payload, strlen(payload));
    if (len == 0) return false;
    if (!esp_cipsend_and_send(pkt, (uint16_t) len, 5000)) return false;
    return true;
}

void StartMqttTask(void *argument) {
    // Attempt connect and loop
    for (;;) {
        if (!mqtt_connect_sequence()) {
            osDelay(3000);
            continue;
        }

        uint32_t last_report = HAL_GetTick();
        uint32_t last_ping = HAL_GetTick();
        bool alive = true;

        while (alive) {
            uint32_t now = HAL_GetTick();
            if (now - last_report >= IOTDA_REPORT_PERIOD_MS) {
                if (!mqtt_publish_properties_tick()) {
                    printf("[MQTT] publish failed, will reconnect\r\n");
                    alive = false; break;
                }
                last_report = now;
            }

            if (now - last_ping >= (IOTDA_KEEPALIVE_SECONDS * 500)) { // keepalive/2 in ms
                uint8_t ping[2]; size_t n = mqtt_build_pingreq(ping, sizeof(ping));
                if (!esp_cipsend_and_send(ping, (uint16_t) n, 3000)) {
                    printf("[MQTT] ping send fail\r\n");
                    alive = false; break;
                }
                // Not strictly required to wait for resp
                mqtt_wait_pingresp(2000);
                last_ping = now;
            }

            // Link closed?
            if (rx_len > 0 && strstr(esp8266_rx_buffer, "CLOSED")) {
                printf("[MQTT] Link closed\r\n");
                alive = false; break;
            }

            osDelay(50);
        }

        // Try close and reconnect
        bsp_esp8266_SendCommand("AT+CIPCLOSE\r\n", "OK", 2000);
        osDelay(1000);
    }
}
