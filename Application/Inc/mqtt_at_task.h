#ifndef INC_MQTT_AT_TASK_H_
#define INC_MQTT_AT_TASK_H_

#include "cmsis_os2.h"

/* ESP01S MQTT（AT）任务入口：
 * - 初始化 ESP01S 联网、SNTP 校时、MQTT 连接
 * - 发布本地投递的开门/开锁事件
 * - 接收云端命令并分发到占位处理函数（后续补齐真实控制逻辑）
 */
void StartMqttAtTask(void *argument);

#endif /* INC_MQTT_AT_TASK_H_ */
