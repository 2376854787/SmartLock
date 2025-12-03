#ifndef INC_TASK_MQTT_H_
#define INC_TASK_MQTT_H_

#include <stdint.h>
#include <stdbool.h>

// WiFi configuration
#ifndef WIFI_SSID
#define WIFI_SSID "123"
#endif

#ifndef WIFI_PASS
#define WIFI_PASS "12345678yan"
#endif

// Huawei Cloud IoTDA MQTT configuration
#ifndef IOTDA_HOST
#define IOTDA_HOST "c78d1f4aef.st1.iotda-device.cn-north-4.myhuaweicloud.com" // e.g. xxx.iot-mqtt.cn-north-4.myhuaweicloud.com
#endif

#ifndef IOTDA_PORT
#define IOTDA_PORT 8883 // 1883 for TCP, 8883 for SSL
#endif

#ifndef IOTDA_USE_SSL
#define IOTDA_USE_SSL 1 // set to 1 to use SSL via ESP8266 "SSL" mode
#endif

#ifndef IOTDA_DEVICE_ID
#define IOTDA_DEVICE_ID "690eb7f29798273cc4fff028_98F4ABf51DBC"
#endif

#ifndef IOTDA_CLIENT_ID
#define IOTDA_CLIENT_ID "690eb7f29798273cc4fff028_98F4ABf51DBC_0_1_2025111015"
#endif

#ifndef IOTDA_USERNAME
#define IOTDA_USERNAME "690eb7f29798273cc4fff028_98F4ABf51DBC"
#endif

#ifndef IOTDA_PASSWORD
#define IOTDA_PASSWORD "70a598e91b7ce67e9dd82c2ba036593ddf38bcfdd697fd1a0919f02ab3ed3ca7"
#endif

#ifndef IOTDA_KEEPALIVE_SECONDS
#define IOTDA_KEEPALIVE_SECONDS 120
#endif

#ifndef IOTDA_REPORT_PERIOD_MS
#define IOTDA_REPORT_PERIOD_MS 30000
#endif

#ifndef IOTDA_SERVICE_ID
#define IOTDA_SERVICE_ID "stm32"
#endif

void StartMqttTask(void *argument);

#endif /* INC_TASK_MQTT_H_ */
