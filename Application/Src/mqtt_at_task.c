// MQTT task using ESP8266 AT+MQTT extended commands (AI-Thinker style)
#include "mqtt_at_task.h"
#include "ESP01S.h"
#include "cmsis_os.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

static bool at_ok(const char *cmd, uint32_t timeout_ms) {
    return bsp_esp8266_SendCommand(cmd, "OK", timeout_ms);
}

static bool mqtt_usercfg(void) {
    // AI-Thinker common format: AT+MQTTUSERCFG=linkid,scheme,"clientID","username","password",clean,keepalive,""
    // Many firmwares accept "NULL" client id here if set by AT+MQTTCLIENTID separately.
    char cmd[512];
    // scheme: 1 often means TCP (non-SSL) on some firmwares, but some place SSL in MQTTCONN.
    // We rely on MQTTCONN last arg to enable SSL; keep scheme=1 here for compatibility.
    snprintf(cmd, sizeof(cmd), "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,%u,\"\"\r\n",
             IOTDA_CLIENT_ID, IOTDA_USERNAME, IOTDA_PASSWORD, (unsigned) IOTDA_KEEPALIVE_SECONDS);
    return at_ok(cmd, 5000);
}

static bool mqtt_clientid(void) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "AT+MQTTCLIENTID=0,\"%s\"\r\n", IOTDA_CLIENT_ID);
    return at_ok(cmd, 3000);
}

static bool mqtt_connect(void) {
    char cmd[512];
    // Common: AT+MQTTCONN=linkid,"host",port,ssl
    // Some firmwares also accept keepalive/clean here; using ssl flag at the end for compatibility.
    snprintf(cmd, sizeof(cmd), "AT+MQTTCONN=0,\"%s\",%d,%d\r\n", IOTDA_HOST, IOTDA_PORT, IOTDA_USE_SSL ? 1 : 0);
    return at_ok(cmd, 20000);
}

static bool mqtt_sub_down(void) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "AT+MQTTSUB=0,\"%s\",1\r\n", IOTDA_TOPIC_DOWN);
    return at_ok(cmd, 5000);
}

static bool mqtt_pub_properties(void) {
    char payload[256];
    // Report property: 温度, service_id configurable
    uint32_t v = (HAL_GetTick() / 1000) % 50 + 10; // 10~59 as demo
    snprintf(payload, sizeof(payload),
             "{\\\"services\\\":[{\\\"service_id\\\":\\\"%s\\\",\\\"properties\\\":{\\\"温度\\\":%lu}}]}",
             IOTDA_SERVICE_ID, (unsigned long)v);
    char cmd[768];
    snprintf(cmd, sizeof(cmd),
             "AT+MQTTPUB=0,\"%s\",\"%s\",0,0\r\n",
             IOTDA_TOPIC_REPORT, payload);
    return at_ok(cmd, 5000);
}

void StartMqttAtTask(void *argument) {
    // Init ESP (UART DMA already armed in freertos.c)
    bsp_esp8266_Init();

    // Join AP
    if (!esp_join_ap(WIFI_SSID, WIFI_PASS, 30000)) {
        printf("[ATMQTT] Join AP failed\r\n");
        // Retry loop
    }

    for (;;) {
        if (!mqtt_usercfg()) { osDelay(1000); continue; }
        if (!mqtt_clientid()) { osDelay(1000); continue; }
        if (!mqtt_connect())  { osDelay(2000); continue; }
        (void) mqtt_sub_down();

        uint32_t last_pub = 0;
        for (;;) {
            uint32_t now = HAL_GetTick();
            if (now - last_pub >= IOTDA_REPORT_PERIOD_MS) {
                if (!mqtt_pub_properties()) {
                    // reconnect on failure
                    break;
                }
                last_pub = now;
            }
            osDelay(50);
        }

        // Close & retry connect sequence
        (void) at_ok("AT+MQTTCLEAN=0\r\n", 3000); // if supported, clean session/disconnect
        osDelay(1000);
    }
}
