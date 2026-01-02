#ifndef SMARTLOCK_HUAWEI_IOT_CONFIG_H
#define SMARTLOCK_HUAWEI_IOT_CONFIG_H

/* 在这里填入你的华为云 IoTDA 设备信息/密钥。 */

#ifndef HUAWEI_IOT_DEVICE_ID
#define HUAWEI_IOT_DEVICE_ID "690eb7f29798273cc4fff028_98F4ABf51DBC"
#endif

#ifndef HUAWEI_IOT_DEVICE_SECRET
#define HUAWEI_IOT_DEVICE_SECRET "1234567890SmartClock"
#endif

/* 区域接入点示例：
 * - iotda.cn-north-4.myhuaweicloud.com（北京4）
 * 请以 IoTDA 控制台显示的区域接入点为准。
 */
#ifndef HUAWEI_IOT_MQTT_HOST
#define HUAWEI_IOT_MQTT_HOST "c78d1f4aef.st1.iotda-device.cn-north-4.myhuaweicloud.com"
#endif

/* 8883 为 TLS；1883 为明文 TCP（不建议用于正式环境）。 */
#ifndef HUAWEI_IOT_MQTT_PORT
#define HUAWEI_IOT_MQTT_PORT 1883u
#endif

/* ESP8266 AT 的 `AT+MQTTUSERCFG` scheme：
 * - 0：MQTT over TCP
 * - 1：MQTT over TLS（取决于 AT 固件是否带 TLS）
 */
#ifndef HUAWEI_IOT_MQTT_SCHEME
/* NOTE: `AT+MQTTUSERCFG` scheme values vary across ESP-AT versions.
 * If `AT+MQTTUSERCFG=...` returns `ERROR`, run `AT+MQTTUSERCFG=?` to confirm.
 * Common mapping: 1=MQTT over TCP, 2=MQTT over TLS.
 */
#define HUAWEI_IOT_MQTT_SCHEME 1u
#endif

/* Dump ESP-AT capabilities on boot (AT+GMR / AT+MQTTUSERCFG=? / AT+MQTTCONN=?). */
#ifndef HUAWEI_IOT_AT_CAP_DUMP
#define HUAWEI_IOT_AT_CAP_DUMP 1u
#endif

/* SNTP 校时配置（ESP8266 AT：AT+CIPSNTPCFG）
 * - 注意：不同 AT 固件支持的 NTP 域名可能不同；如果 `pool.ntp.org` 解析慢/不通，建议改成国内域名。
 */
#ifndef HUAWEI_IOT_NTP_SERVER
#define HUAWEI_IOT_NTP_SERVER "ntp.aliyun.com"
#endif

/* 时区：东八区为 8 */
#ifndef HUAWEI_IOT_TIMEZONE
#define HUAWEI_IOT_TIMEZONE 8
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
#define HUAWEI_IOT_PASSWORD_BASE64 0u
#endif

/* IoTDA clientId 第 3 段：密码签名类型
 * - 0：HMACSHA256 不校验时间戳（仍需携带时间戳）
 * - 1：HMACSHA256 校验时间戳
 */
#ifndef HUAWEI_IOT_AUTH_SIGN_TYPE
#define HUAWEI_IOT_AUTH_SIGN_TYPE 1u
#endif

#endif /* SMARTLOCK_HUAWEI_IOT_CONFIG_H */
