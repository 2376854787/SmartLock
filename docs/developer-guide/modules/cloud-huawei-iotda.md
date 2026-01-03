# 模块指南：华为云 IoTDA（ESP-01S + ESP-AT + MQTT）

本模块实现：

- ESP-01S 联网（ESP-AT）
- SNTP 校时（用于 IoTDA 鉴权签名的时间戳）
- MQTT 连接 IoTDA
- 订阅云端命令、上报 door 事件、回包 ack/response

相关路径：

- 配置：`Application/Inc/huawei_iot_config.h`
- 鉴权/topic 生成：`Application/Inc/huawei_iot.h`、`Application/Src/huawei_iot.c`
- MQTT 任务：`Application/Src/mqtt_at_task.c`
- “邮箱队列”（任务间解耦）：`Application/Inc/wifi_mqtt_task.h`、`Application/Src/wifi_mqtt_task.c`
- AT 框架（USART3）：`components/AT/*`

## 1. 硬件链路

- ESP-01S 连接 USART3（PB10=TX / PB11=RX，见 `SmartLock.ioc`）
- AT 栈与 MQTT 由 `components/AT` + `Application/Src/mqtt_at_task.c` 驱动

## 2. 启动链路（谁创建谁）

1) `Core/Src/freertos.c` 调用 `at_core_task_init(&g_at_manager, &huart3)`
2) AT core 线程就绪后，在 `components/AT/AT_Core_Task.c` 内部创建 `MQTT_AT` 任务
3) `MQTT_AT` 任务在 `Application/Src/mqtt_at_task.c:StartMqttAtTask()` 中完成：
   - `esp01s_Init()` 联网
   - `AT+CIPSNTPCFG` / `AT+CIPSNTPTIME?` 校时
   - `AT+MQTTUSERCFG` / `AT+MQTTCONN` 连接 IoTDA
   - `AT+MQTTSUB` 订阅 topic

## 3. Topic 与协议（固件实现说明）

固件同时支持两套命令通道：

1) user topic（推荐联调/后端对接）：`key=value` 载荷（不使用 JSON）
2) IoTDA 标准 `sys/commands`：JSON 命令与 JSON response

完整协议见：

- `docs/mqtt-control.md`

## 4. 云端开锁/关锁的执行逻辑（固件侧）

### 4.1 收到 user topic 命令（`cmd=unlock` / `cmd=lock`）

代码入口：`Application/Src/mqtt_at_task.c:handle_user_command()`

- `cmd=unlock`：
  - 调用 `LockActuator_UnlockAsync()`（投递到执行器队列）
  - 上报 door=open（method=cloud）
  - 发布 `user/cmd/ack`（`unlock_accepted`）
- `cmd=lock`：
  - 调用 `LockActuator_LockAsync()`
  - 上报 door=close（method=cloud）
  - 发布 `user/cmd/ack`（`lock_accepted`）

### 4.2 收到 IoTDA 标准命令（`command_name=unlock/lock`）

代码入口：`Application/Src/mqtt_at_task.c:handle_iotda_sys_command()`

- `unlock` / `lock` 的执行动作与 user topic 一致
- 返回 JSON response（见 `docs/mqtt-control.md`）

## 5. 事件上报（door event）

door 事件通过 `wifi_mqtt_report_door_event()` 投递到邮箱队列，然后由 MQTT 任务发布。

字段与示例见 `docs/mqtt-control.md`。

## 6. 常见问题（联调排查）

- IoTDA 连不上：先确认 SNTP 成功（`AT+CIPSNTPTIME?` 有回包）
- 命令收到了但门不动：检查执行器任务是否启动（`LockActuator_Start()`）与队列是否满
- 只收到了 ack 没有事件：确认设备是否有上报 door event（topic 是否订阅/发布成功）

