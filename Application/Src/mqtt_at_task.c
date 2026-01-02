#include "mqtt_at_task.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "APP_config.h"
#include "ESP01S.h"
#include "AT.h"
#include "AT_Core_Task.h"
#include "huawei_iot.h"
#include "log.h"
#include "lock_data.h"
#include "osal.h"
#include "usart.h"
#include "wifi_mqtt_task.h"

/* 说明：
 * - 本文件用于“先把华为云 IoTDA 跑通”的落脚点（ESP8266 AT + MQTT）。
 * - 云端命令处理目前仅做占位（留 TODO 挂钩），后续再接入真实开锁/关锁逻辑。
 */

typedef struct {
    volatile uint8_t mqtt_connected;
    volatile uint8_t have_sntp_epoch;
    uint32_t sntp_epoch_s;
} mqtt_at_ctx_t;

static mqtt_at_ctx_t s_ctx;

static bool str_starts_with(const char *s, const char *prefix)
{
    if (!s || !prefix) return false;
    const size_t n = strlen(prefix);
    return strncmp(s, prefix, n) == 0;
}

static bool parse_quoted_field(const char **p_io, char *out, size_t out_sz)
{
    if (!p_io || !*p_io || !out || out_sz == 0) return false;
    const char *p = *p_io;
    while (*p == ' ' || *p == ',') p++;
    if (*p != '\"') return false;
    p++;
    size_t i = 0;
    while (*p && *p != '\"') {
        if (i + 1u < out_sz) out[i++] = *p;
        p++;
    }
    if (*p != '\"') return false;
    out[i] = '\0';
    p++; /* skip closing quote */
    *p_io = p;
    return true;
}

static uint8_t parse_u8(const char **p_io)
{
    const char *p = *p_io;
    while (*p == ' ' || *p == ',') p++;
    uint32_t v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10u + (uint32_t)(*p - '0');
        p++;
    }
    *p_io = p;
    if (v > 255u) v = 255u;
    return (uint8_t)v;
}

static void wait_sntp_epoch(uint32_t timeout_ms)
{
    const uint32_t freq = osKernelGetTickFreq();
    const uint32_t start = osKernelGetTickCount();
    if (freq == 0u) return;

    const uint32_t timeout_ticks = (uint32_t)(((uint64_t)timeout_ms * (uint64_t)freq + 999ull) / 1000ull);

    while (!s_ctx.have_sntp_epoch) {
        const uint32_t now = osKernelGetTickCount();
        if ((uint32_t)(now - start) >= timeout_ticks) break;
        osDelay(50);
    }
}

static void urc_handler(AT_Manager_t *mgr, const char *line, void *user)
{
    (void)mgr;
    mqtt_at_ctx_t *ctx = (mqtt_at_ctx_t *)user;
    if (!ctx || !line) return;

    /* MQTT 连接状态 */
    if (strstr(line, "+MQTTCONNECTED:")) {
        ctx->mqtt_connected = 1;
        LOG_I("mqtt", "connected");
        return;
    }
    if (strstr(line, "+MQTTDISCONNECTED:")) {
        ctx->mqtt_connected = 0;
        LOG_W("mqtt", "disconnected");
        return;
    }

    /* SNTP 时间响应（通常来自 AT+CIPSNTPTIME? 的中间行） */
    if (strstr(line, "+CIPSNTPTIME:")) {
        uint32_t epoch_s = 0;
        if (huawei_iot_parse_sntp_time_to_epoch_s(line, &epoch_s)) {
            ctx->sntp_epoch_s = epoch_s;
            ctx->have_sntp_epoch = 1;
            lock_time_set_epoch_s(epoch_s);
            LOG_I("time", "SNTP epoch=%lu", (unsigned long)epoch_s);
        } else {
            LOG_W("time", "SNTP parse failed: %s", line);
        }
        return;
    }

    /* MQTT 下行消息 */
    if (str_starts_with(line, "+MQTTSUBRECV:")) {
        /* 期望格式（ESP8266 AT）：+MQTTSUBRECV:<linkid>,\"topic\",\"payload\" */
        const char *p = line + strlen("+MQTTSUBRECV:");
        (void)parse_u8(&p); /* link id */

        char topic[WIFI_MQTT_TOPIC_MAX];
        char payload[WIFI_MQTT_PAYLOAD_MAX];
        topic[0] = '\0';
        payload[0] = '\0';
        if (!parse_quoted_field(&p, topic, sizeof(topic))) return;
        if (!parse_quoted_field(&p, payload, sizeof(payload))) {
            /* 部分固件 payload 可能不带引号：这里退化为“取剩余整行”。 */
            while (*p == ' ' || *p == ',') p++;
            (void)snprintf(payload, sizeof(payload), "%s", p);
        }

        wifi_mqtt_msg_t msg;
        memset(&msg, 0, sizeof(msg));
        msg.type = WIFI_MQTT_MSG_CLOUD_RX;
        msg.ts_s = lock_time_now_s();
        (void)snprintf(msg.topic, sizeof(msg.topic), "%s", topic);
        (void)snprintf(msg.payload, sizeof(msg.payload), "%s", payload);
        (void)wifi_mqtt_mailbox_offer(&msg, 0);
        return;
    }
}

static bool at_send_ok(const char *cmd, uint32_t timeout_ms)
{
    const AT_Resp_t r = AT_SendCmd(&g_at_manager, cmd, "OK", timeout_ms);
    if (r != AT_RESP_OK) {
        LOG_W("AT", "cmd failed r=%d cmd=%s", (int)r, cmd);
        return false;
    }
    return true;
}

static bool at_send_ok_retry(const char *cmd, uint32_t timeout_ms, uint8_t retries, uint32_t retry_delay_ms)
{
    for (uint8_t i = 0; i <= retries; i++) {
        const AT_Resp_t r = AT_SendCmd(&g_at_manager, cmd, "OK", timeout_ms);
        if (r == AT_RESP_OK) return true;
        LOG_W("AT", "cmd failed r=%d try=%u cmd=%s", (int)r, (unsigned)i, cmd);
        osDelay(retry_delay_ms);
    }
    return false;
}

static void at_dump_esp_at_caps(void)
{
#if defined(HUAWEI_IOT_AT_CAP_DUMP) && (HUAWEI_IOT_AT_CAP_DUMP)
    static uint8_t dumped = 0;
    if (dumped) return;
    dumped = 1;

    LOG_I("AT", "ESP-AT cap dump: AT+GMR");
    (void)AT_SendCmd(&g_at_manager, "AT+GMR\r\n", "OK", 3000);
    LOG_I("AT", "ESP-AT cap dump: AT+MQTTUSERCFG=?");
    (void)AT_SendCmd(&g_at_manager, "AT+MQTTUSERCFG=?\r\n", "OK", 3000);
    LOG_I("AT", "ESP-AT cap dump: AT+MQTTCONN=?");
    (void)AT_SendCmd(&g_at_manager, "AT+MQTTCONN=?\r\n", "OK", 3000);
#endif
}

static bool mqtt_usercfg_with_scheme(uint8_t scheme, const char *client_id, const char *username, const char *password)
{
    char cmd[AT_CMD_MAX_LEN];
    const int n = snprintf(cmd, sizeof(cmd),
                           "AT+MQTTUSERCFG=0,%u,\"%s\",\"%s\",\"%s\",0,0,\"\"\r\n",
                           (unsigned)scheme,
                           client_id,
                           username,
                           password);
    if (n <= 0 || (size_t)n >= sizeof(cmd)) return false;
    return at_send_ok_retry(cmd, 10000, 3, 500);
}

static bool mqtt_usercfg_auto(const char *client_id, const char *username, const char *password)
{
    const uint8_t candidates[] = {
        (uint8_t)HUAWEI_IOT_MQTT_SCHEME,
        2u,
        1u,
        0u,
    };

    for (size_t i = 0; i < (sizeof(candidates) / sizeof(candidates[0])); i++) {
        const uint8_t scheme = candidates[i];
        bool dup = false;
        for (size_t j = 0; j < i; j++) {
            if (candidates[j] == scheme) {
                dup = true;
                break;
            }
        }
        if (dup) continue;

        LOG_I("mqtt", "MQTTUSERCFG try scheme=%u", (unsigned)scheme);
        if (mqtt_usercfg_with_scheme(scheme, client_id, username, password)) return true;
        LOG_W("mqtt", "MQTTUSERCFG scheme=%u rejected, try next", (unsigned)scheme);
    }

    return false;
}

static bool mqtt_setup_and_connect(uint32_t epoch_s_utc)
{
    char client_id[128];
    char username[96];
    char password[128];
    if (!huawei_iot_build_mqtt_auth(epoch_s_utc, client_id, sizeof(client_id), username, sizeof(username), password,
                                   sizeof(password))) {
        LOG_E("huawei", "build auth failed");
        return false;
    }

    at_dump_esp_at_caps();
    if (!mqtt_usercfg_auto(client_id, username, password)) return false;

    char cmd[AT_CMD_MAX_LEN];
    const int n2 = snprintf(cmd, sizeof(cmd),
                   "AT+MQTTCONN=0,\"%s\",%u,1\r\n",
                   HUAWEI_IOT_MQTT_HOST,
                   (unsigned)HUAWEI_IOT_MQTT_PORT);
    if (n2 <= 0 || (size_t)n2 >= sizeof(cmd)) return false;
    if (!at_send_ok_retry(cmd, 20000, 3, 800)) return false;

    /* 订阅华为云 IoTDA 命令 Topic（下行）。 */
    char sub_topic[WIFI_MQTT_TOPIC_MAX];
    huawei_iot_build_cmd_sub_topic(sub_topic, sizeof(sub_topic));
    const int n3 = snprintf(cmd, sizeof(cmd), "AT+MQTTSUB=0,\"%s\",1\r\n", sub_topic);
    if (n3 > 0 && (size_t)n3 < sizeof(cmd)) {
        (void)at_send_ok(cmd, 10000);
    }

    /* 订阅自定义 user/cmd：用于你在云端按本文档直接下发控制命令。 */
    char user_cmd[WIFI_MQTT_TOPIC_MAX];
    huawei_iot_build_user_cmd_topic(user_cmd, sizeof(user_cmd));
    const int n4 = snprintf(cmd, sizeof(cmd), "AT+MQTTSUB=0,\"%s\",1\r\n", user_cmd);
    if (n4 > 0 && (size_t)n4 < sizeof(cmd)) {
        (void)at_send_ok(cmd, 10000);
    }

    return true;
}

static bool mqtt_publish(const char *topic, const char *payload)
{
    if (!topic || !payload) return false;
    char cmd[AT_CMD_MAX_LEN];
    const int n = snprintf(cmd, sizeof(cmd),
                   "AT+MQTTPUB=0,\"%s\",\"%s\",1,0\r\n",
                   topic,
                   payload);
    if (n <= 0 || (size_t)n >= sizeof(cmd)) return false;
    return at_send_ok(cmd, 12000);
}

static bool mqtt_publish_raw(const char *topic, const char *payload, size_t payload_len)
{
    if (!topic || !payload) return false;
    if (payload_len == 0) return false;

    /* ESP-AT: publish raw payload without quoting/escaping. */
    char cmd[AT_CMD_MAX_LEN];
    const int n = snprintf(cmd, sizeof(cmd),
                           "AT+MQTTPUBRAW=0,\"%s\",%u,1,0\r\n",
                           topic,
                           (unsigned)payload_len);
    if (n <= 0 || (size_t)n >= sizeof(cmd)) {
        LOG_E("mqtt", "MQTTPUBRAW cmd too long (topic=%u len=%u)",
              (unsigned)strlen(topic),
              (unsigned)payload_len);
        return false;
    }

    const AT_Resp_t r1 = AT_SendCmd(&g_at_manager, cmd, ">", 5000);
    if (r1 != AT_RESP_OK) {
        LOG_E("mqtt", "MQTTPUBRAW no prompt r=%d", (int)r1);
        return false;
    }

    /* Send exactly <payload_len> bytes. Our payload is a C string without NUL. */
    if (strlen(payload) != payload_len) {
        LOG_E("mqtt", "MQTTPUBRAW len mismatch strlen=%u payload_len=%u",
              (unsigned)strlen(payload),
              (unsigned)payload_len);
        return false;
    }

    const AT_Resp_t r2 = AT_SendCmd(&g_at_manager, payload, "OK", 12000);
    if (r2 != AT_RESP_OK) {
        LOG_E("mqtt", "MQTTPUBRAW payload send failed r=%d", (int)r2);
        return false;
    }

    return true;
}

static bool mqtt_publish_json(const char *topic, const char *json_payload)
{
    if (!topic || !json_payload) return false;

    /* JSON contains quotes, which `AT+MQTTPUB` cannot safely carry on this ESP-AT build.
     * Use RAW publish instead.
     */
    const size_t len = strlen(json_payload);
    if (len == 0 || len >= WIFI_MQTT_PAYLOAD_MAX) return false;
    return mqtt_publish_raw(topic, json_payload, len);
}

static uint32_t prng_next_u32(uint32_t *state)
{
    uint32_t x = state ? *state : 0x12345678u;
    x = x * 1664525u + 1013904223u;
    if (state) *state = x;
    return x;
}

static bool iotda_report_temperature_once(void)
{
    char topic[WIFI_MQTT_TOPIC_MAX];
    huawei_iot_build_up_topic(topic, sizeof(topic));

    uint32_t seed = (uint32_t)osKernelGetTickCount();
    const int temp = (int)(prng_next_u32(&seed) % 11u) + 25; /* 25..35 */

    char json[WIFI_MQTT_PAYLOAD_MAX];
    const int n = snprintf(json, sizeof(json),
                           "{\"services\":[{\"service_id\":\"%s\",\"properties\":{\"%s\":%d}}]}",
                           HUAWEI_IOT_SERVICE_ID,
                           HUAWEI_IOT_PROP_TEMP_NAME,
                           temp);
    if (n <= 0 || (size_t)n >= sizeof(json)) return false;

    LOG_I("mqtt", "report property 温度=%d service=%s", temp, HUAWEI_IOT_SERVICE_ID);
    return mqtt_publish_json(topic, json);
}

static bool kv_get(const char *s, const char *key, char *out, size_t out_sz)
{
    if (!s || !key || !out || out_sz == 0) return false;
    out[0] = '\0';

    const size_t klen = strlen(key);
    const char *p = s;
    while (*p) {
        while (*p == ' ' || *p == ',') p++;
        const char *seg = p;
        const char *eq = strchr(seg, '=');
        if (!eq) return false;
        const size_t name_len = (size_t)(eq - seg);
        if (name_len == klen && strncmp(seg, key, klen) == 0) {
            const char *v = eq + 1;
            const char *end = strchr(v, ',');
            size_t vlen = end ? (size_t)(end - v) : strlen(v);
            if (vlen >= out_sz) vlen = out_sz - 1u;
            memcpy(out, v, vlen);
            out[vlen] = '\0';
            return true;
        }
        const char *next = strchr(p, ',');
        if (!next) break;
        p = next + 1;
    }
    return false;
}

static bool topic_is_iotda_sys_command(const char *topic)
{
    if (!topic) return false;
    return strstr(topic, "/sys/commands/") != NULL;
}

static bool topic_is_user_cmd(const char *topic)
{
    char t[WIFI_MQTT_TOPIC_MAX];
    huawei_iot_build_user_cmd_topic(t, sizeof(t));
    return (topic && strcmp(topic, t) == 0);
}

static void publish_user_cmd_ack(uint32_t result_code, const char *result_desc)
{
    char ack_topic[WIFI_MQTT_TOPIC_MAX];
    huawei_iot_build_user_cmd_ack_topic(ack_topic, sizeof(ack_topic));

    char payload[WIFI_MQTT_PAYLOAD_MAX];
    (void)snprintf(payload, sizeof(payload),
                   "result_code=%lu,result_desc=%s,ts=%lu",
                   (unsigned long)result_code,
                   (result_desc && result_desc[0]) ? result_desc : "OK",
                   (unsigned long)lock_time_now_s());
    (void)mqtt_publish(ack_topic, payload);
}

static void handle_user_command(const char *payload)
{
    char cmd[32];
    if (!kv_get(payload, "cmd", cmd, sizeof(cmd))) {
        publish_user_cmd_ack(2, "missing_cmd");
        return;
    }

    if (strcmp(cmd, "ping") == 0) {
        publish_user_cmd_ack(0, "pong");
        return;
    }

    if (strcmp(cmd, "temp") == 0) {
        (void)iotda_report_temperature_once();
        publish_user_cmd_ack(0, "temp_reported");
        return;
    }

    if (strcmp(cmd, "time_sync") == 0) {
        (void)at_send_ok_retry("AT+CIPSNTPTIME?\r\n", 3000, 1, 500);
        publish_user_cmd_ack(0, "time_sync_started");
        return;
    }

    if (strcmp(cmd, "unlock") == 0) {
        (void)wifi_mqtt_report_unlock_event(WIFI_MQTT_UNLOCK_CLOUD);
        publish_user_cmd_ack(0, "unlock_accepted");
        return;
    }

    if (strcmp(cmd, "door") == 0) {
        char state[16];
        if (!kv_get(payload, "state", state, sizeof(state))) {
            publish_user_cmd_ack(2, "missing_state");
            return;
        }
        const bool is_open = (strcmp(state, "open") == 0) || (strcmp(state, "1") == 0);
        (void)wifi_mqtt_report_door_event(is_open, WIFI_MQTT_UNLOCK_CLOUD);
        publish_user_cmd_ack(0, "door_event_accepted");
        return;
    }

    /* 其他命令先占位，后续补齐实际控制逻辑 */
    publish_user_cmd_ack(1, "todo");
}

static void handle_cloud_command_placeholder(const char *topic, const char *payload)
{
    /* TODO：解析 IoTDA 命令内容，并映射到实际门锁控制逻辑。 */
    LOG_W("cloud", "RX topic=%s payload=%s", topic ? topic : "(null)", payload ? payload : "(null)");

    /* 尽力而为：先对命令请求回一个占位 ACK，后续补齐真实结果。 */
    char resp_topic[WIFI_MQTT_TOPIC_MAX];
    if (!huawei_iot_build_cmd_resp_topic_from_request(topic, resp_topic, sizeof(resp_topic))) {
        return;
    }
    (void)mqtt_publish(resp_topic, "result_code=0,result_desc=TODO");
}

void StartMqttAtTask(void *argument)
{
    (void)argument;

    memset(&s_ctx, 0, sizeof(s_ctx));
    wifi_mqtt_mailbox_init();
    AT_SetUrcHandler(&g_at_manager, urc_handler, &s_ctx);

    /* 拉起 Wi-Fi（必要时走 SmartConfig）。 */
    osDelay(1500);
    esp01s_Init(&huart3, 1024);
    osDelay(200);
    at_dump_esp_at_caps();

    /* 校时：用于基于时间戳的鉴权/签名。 */
    {
        char sntp_cfg[AT_CMD_MAX_LEN];
        (void)snprintf(sntp_cfg, sizeof(sntp_cfg),
                       "AT+CIPSNTPCFG=1,%d,\"%s\"\r\n",
                       (int)HUAWEI_IOT_TIMEZONE,
                       HUAWEI_IOT_NTP_SERVER);
        (void)at_send_ok_retry(sntp_cfg, 8000, 2, 300);
    }

    /* 注意：部分固件在刚配置 SNTP 后立即查询会“卡很久”，这里限定等待时间，超时则继续走本地 uptime 时间戳。 */
    osDelay(1500);
    LOG_W("time", "SNTP query start");
    (void)at_send_ok_retry("AT+CIPSNTPTIME?\r\n", 3000, 1, 500);
    wait_sntp_epoch(6000);
    if (!s_ctx.have_sntp_epoch) {
        LOG_W("time", "SNTP not ready, continue with local uptime timestamp");
    } else {
        LOG_W("time", "SNTP ready epoch=%lu", (unsigned long)s_ctx.sntp_epoch_s);
    }

    /* 连接华为云 IoTDA（MQTT）。 */
    if (!lock_time_has_epoch()) {
        LOG_E("mqtt", "no SNTP epoch, skip connect");
        return;
    }

    const uint32_t epoch_s_utc = lock_time_now_s();
    if (!mqtt_setup_and_connect(epoch_s_utc)) {
        LOG_E("mqtt", "connect failed (check iot config/firmware)");
    }

    /* Demo: report one random temperature property so IoTDA console can refresh. */
    if (s_ctx.mqtt_connected) {
        (void)iotda_report_temperature_once();
    }

    for (;;) {
        wifi_mqtt_msg_t msg;
        if (!wifi_mqtt_mailbox_take(&msg, 1000)) {
            continue;
        }

        if (msg.type == WIFI_MQTT_MSG_PUBLISH) {
            char up_topic[WIFI_MQTT_TOPIC_MAX];
            huawei_iot_build_user_door_event_topic(up_topic, sizeof(up_topic));
            (void)mqtt_publish(up_topic, msg.payload);
            continue;
        }

        if (msg.type == WIFI_MQTT_MSG_CLOUD_RX) {
            if (topic_is_user_cmd(msg.topic)) {
                handle_user_command(msg.payload);
            } else if (topic_is_iotda_sys_command(msg.topic)) {
                handle_cloud_command_placeholder(msg.topic, msg.payload);
            } else {
                LOG_W("cloud", "unhandled topic=%s payload=%s", msg.topic, msg.payload);
            }
            continue;
        }
    }
}
