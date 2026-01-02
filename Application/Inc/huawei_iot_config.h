#ifndef SMARTLOCK_HUAWEI_IOT_CONFIG_H
#define SMARTLOCK_HUAWEI_IOT_CONFIG_H

/* 在这里填入你的华为云 IoTDA 设备信息/密钥。 */

#ifndef HUAWEI_IOT_DEVICE_ID
#define HUAWEI_IOT_DEVICE_ID "TODO_DEVICE_ID"
#endif

#ifndef HUAWEI_IOT_DEVICE_SECRET
#define HUAWEI_IOT_DEVICE_SECRET "TODO_DEVICE_SECRET"
#endif

/* 区域接入点示例：
 * - iotda.cn-north-4.myhuaweicloud.com（北京4）
 * 请以 IoTDA 控制台显示的区域接入点为准。
 */
#ifndef HUAWEI_IOT_MQTT_HOST
#define HUAWEI_IOT_MQTT_HOST "iotda.cn-north-4.myhuaweicloud.com"
#endif

/* 8883 为 TLS；1883 为明文 TCP（不建议用于正式环境）。 */
#ifndef HUAWEI_IOT_MQTT_PORT
#define HUAWEI_IOT_MQTT_PORT 8883u
#endif

/* ESP8266 AT 的 `AT+MQTTUSERCFG` scheme：
 * - 0：MQTT over TCP
 * - 1：MQTT over TLS（取决于 AT 固件是否带 TLS）
 */
#ifndef HUAWEI_IOT_MQTT_SCHEME
#define HUAWEI_IOT_MQTT_SCHEME 1u
#endif

/* 属性上报的默认 service_id（可改成与你产品模型一致）。 */
#ifndef HUAWEI_IOT_SERVICE_ID
#define HUAWEI_IOT_SERVICE_ID "SmartLock"
#endif

/* password 编码方式：
 * - 1：Base64(HMAC-SHA256)
 * - 0：Hex(HMAC-SHA256)
 */
#ifndef HUAWEI_IOT_PASSWORD_BASE64
#define HUAWEI_IOT_PASSWORD_BASE64 1u
#endif

#endif /* SMARTLOCK_HUAWEI_IOT_CONFIG_H */
