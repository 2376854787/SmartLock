#include "wifi_mqtt_task.h"

#include <stdio.h>
#include <string.h>

#include "APP_config.h"
#include "log.h"
#include "lock_data.h"
#include "osal.h"

/* 同一个邮箱队列同时承载：上行发布与云端下行消息。
 * 合并成一个队列可以简化 MQTT 任务的循环处理逻辑。
 */
#ifndef WIFI_MQTT_MAILBOX_DEPTH
#define WIFI_MQTT_MAILBOX_DEPTH 8u
#endif

static osal_msgq_t s_mbox = NULL;
static volatile uint8_t s_ready = 0;

static uint32_t now_s(void)
{
    return lock_time_now_s();
}

void wifi_mqtt_mailbox_init(void)
{
    if (s_mbox) return;
    if (ret_is_err(OSAL_msgq_create(&s_mbox, "wifi_mqtt", sizeof(wifi_mqtt_msg_t), WIFI_MQTT_MAILBOX_DEPTH))) {
        LOG_E("wifi_mqtt", "mailbox create failed");
        s_mbox = NULL;
        s_ready = 0;
        return;
    }
    s_ready = 1;
}

bool wifi_mqtt_mailbox_take(wifi_mqtt_msg_t *out, uint32_t timeout_ms)
{
    if (!out || !s_ready || !s_mbox) return false;
    return ret_is_ok(OSAL_msgq_get(s_mbox, out, timeout_ms));
}

bool wifi_mqtt_mailbox_offer(const wifi_mqtt_msg_t *msg, uint32_t timeout_ms)
{
    if (!msg || !s_ready || !s_mbox) return false;
    return ret_is_ok(OSAL_msgq_put(s_mbox, (void *)msg, timeout_ms));
}

static const char *method_to_str(wifi_mqtt_unlock_method_t m)
{
    switch (m) {
        case WIFI_MQTT_UNLOCK_PIN: return "pin";
        case WIFI_MQTT_UNLOCK_RFID: return "rfid";
        case WIFI_MQTT_UNLOCK_FINGERPRINT: return "fingerprint";
        case WIFI_MQTT_UNLOCK_CLOUD: return "cloud";
        default: return "unknown";
    }
}

bool wifi_mqtt_report_unlock_event(wifi_mqtt_unlock_method_t method)
{
    return wifi_mqtt_report_door_event(true, method);
}

bool wifi_mqtt_report_door_event(bool is_open, wifi_mqtt_unlock_method_t method)
{
    if (!s_ready) return false;

    wifi_mqtt_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = WIFI_MQTT_MSG_PUBLISH;
    msg.ts_s = now_s();

    /* topic 由 mqtt task 统一决定（华为云 Topic 与 device_id 相关）。 */
    (void)snprintf(msg.topic, sizeof(msg.topic), "door/event");

    /* 注意：除非切到 RAW 发布模式，否则 payload 不要包含双引号，避免 AT 指令拼接出错。 */
    (void)snprintf(msg.payload, sizeof(msg.payload),
                   "door=%s,method=%s,ts=%lu",
                   is_open ? "open" : "close",
                   method_to_str(method),
                   (unsigned long)msg.ts_s);

    /* 为了 UI 不被阻塞：这里使用非阻塞投递，队列满则丢弃。 */
    return wifi_mqtt_mailbox_offer(&msg, 0);
}
