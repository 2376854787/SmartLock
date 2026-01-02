#ifndef INC_WIFI_MQTT_TASK_H_
#define INC_WIFI_MQTT_TASK_H_

#include <stdbool.h>
#include <stdint.h>

/* 本模块仅提供轻量“邮箱队列”，用于任务间解耦：
 * - 本地开门/开锁事件 -> MQTT 发送任务
 * - 云端 MQTT 下行消息 -> MQTT 任务（命令分发）
 *
 * 注意：本模块不直接发送 AT 指令（避免阻塞 UI/其他任务）。
 */

typedef enum {
    WIFI_MQTT_UNLOCK_UNKNOWN = 0,
    WIFI_MQTT_UNLOCK_PIN,
    WIFI_MQTT_UNLOCK_RFID,
    WIFI_MQTT_UNLOCK_FINGERPRINT,
    WIFI_MQTT_UNLOCK_CLOUD,
} wifi_mqtt_unlock_method_t;

/* 投递一次“开锁 => 门已打开”事件。
 * 若 MQTT 任务未就绪或队列已满则返回 false。
 */
bool wifi_mqtt_report_unlock_event(wifi_mqtt_unlock_method_t method);

/* 投递一次门状态事件（开/关）。
 * 若 MQTT 任务未就绪或队列已满则返回 false。
 */
bool wifi_mqtt_report_door_event(bool is_open, wifi_mqtt_unlock_method_t method);

/* ---- 供 mqtt_at_task.c 使用的内部接口 ---- */

typedef enum {
    WIFI_MQTT_MSG_PUBLISH = 1,
    WIFI_MQTT_MSG_CLOUD_RX = 2,
} wifi_mqtt_msg_type_t;

#ifndef WIFI_MQTT_TOPIC_MAX
#define WIFI_MQTT_TOPIC_MAX 128u
#endif

#ifndef WIFI_MQTT_PAYLOAD_MAX
#define WIFI_MQTT_PAYLOAD_MAX 256u
#endif

typedef struct {
    wifi_mqtt_msg_type_t type;
    uint32_t ts_s;
    char topic[WIFI_MQTT_TOPIC_MAX];
    char payload[WIFI_MQTT_PAYLOAD_MAX];
} wifi_mqtt_msg_t;

void wifi_mqtt_mailbox_init(void);
bool wifi_mqtt_mailbox_take(wifi_mqtt_msg_t *out, uint32_t timeout_ms);
bool wifi_mqtt_mailbox_offer(const wifi_mqtt_msg_t *msg, uint32_t timeout_ms);

#endif /* INC_WIFI_MQTT_TASK_H_ */
