#ifndef SMARTLOCK_HUAWEI_IOT_H
#define SMARTLOCK_HUAWEI_IOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "huawei_iot_config.h"

#ifdef __cplusplus
extern "C" {
#endif

uint64_t huawei_iot_timestamp_ms(void);

/* Huawei IoTDA MQTT auth:
 * - ClientId: <device_id>_0_<sign_type>_<YYYYMMDDHH> (UTC)
 * - Username: <device_id>
 * - Password: HMAC-SHA256(key=YYYYMMDDHH, msg=secret), output as hex string
 */
bool huawei_iot_build_mqtt_auth(uint32_t epoch_s_utc,
                                char *out_client_id,
                                size_t client_id_sz,
                                char *out_username,
                                size_t username_sz,
                                char *out_password,
                                size_t password_sz);

void huawei_iot_build_up_topic(char *out, size_t out_sz);
void huawei_iot_build_cmd_sub_topic(char *out, size_t out_sz);
void huawei_iot_build_user_door_event_topic(char *out, size_t out_sz);
void huawei_iot_build_user_cmd_topic(char *out, size_t out_sz);
void huawei_iot_build_user_cmd_ack_topic(char *out, size_t out_sz);

/* IoTDA 命令 Topic 说明：
 * - request： $oc/devices/<device_id>/sys/commands/<request_id>/request
 * - response：$oc/devices/<device_id>/sys/commands/<request_id>/response
 */
bool huawei_iot_build_cmd_resp_topic_from_request(const char *request_topic, char *out, size_t out_sz);

/* 解析 ESP8266 AT 的 SNTP 响应：
 * - +CIPSNTPTIME:"Thu Nov 05 23:02:10 2020"
 */
bool huawei_iot_parse_sntp_time_to_epoch_s(const char *line, uint32_t *out_epoch_s);

#ifdef __cplusplus
}
#endif

#endif /* SMARTLOCK_HUAWEI_IOT_H */
