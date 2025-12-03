#ifndef INC_MQTT_AT_TASK_H_
#define INC_MQTT_AT_TASK_H_

#include <stdint.h>

// Reuse connection parameters from wifi_mqtt_task.h if available
#include "wifi_mqtt_task.h"

// Topic helpers for Huawei IoTDA
#ifndef IOTDA_TOPIC_REPORT
#define IOTDA_TOPIC_REPORT "$oc/devices/" IOTDA_DEVICE_ID "/sys/properties/report"
#endif

#ifndef IOTDA_TOPIC_DOWN
#define IOTDA_TOPIC_DOWN   "$oc/devices/" IOTDA_DEVICE_ID "/sys/messages/down"
#endif

void StartMqttAtTask(void *argument);

#endif // INC_MQTT_AT_TASK_H_

