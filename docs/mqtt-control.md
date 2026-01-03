# MQTT 控制命令协议（云端交互用）

本文档给 Flutter/后端使用：描述“如何用 MQTT 控制门锁、如何解析设备上报事件”。  
文档内容以固件实现为准：命令解析/上报在 `Application/Src/mqtt_at_task.c` 与 `Application/Src/wifi_mqtt_task.c`。

## 0. 版本与约束

- 协议版本：`user topic + key=value`（当前固件默认）
- 字符集：建议只用 ASCII（字母/数字/下划线/逗号/等号/点号）
- 载荷格式：`k=v,k2=v2`（避免 ESP-AT `AT+MQTTPUB` 的 JSON/引号转义复杂度）
- 时间戳：`ts` 为 **秒**；若 SNTP 成功为 epoch 秒，否则为上电秒（由 `lock_time_now_s()` 提供）

## 1. Topic（主题）

以下 `<device_id>` 来自 `Application/Inc/huawei_iot_config.h` 的 `HUAWEI_IOT_DEVICE_ID`。

### 1.1 设备 -> 云：门事件上报

- `\$oc/devices/<device_id>/user/events/door`

### 1.2 云 -> 设备：用户自定义命令下发

- `\$oc/devices/<device_id>/user/cmd`

### 1.3 设备 -> 云：用户自定义命令应答

- `\$oc/devices/<device_id>/user/cmd/ack`

### 1.4 IoTDA 标准命令（可选）

固件也会订阅 IoTDA 标准命令 topic（用于控制台“下发命令”）：

- `\$oc/devices/<device_id>/sys/commands/#`

对应响应 topic 由固件根据请求 topic 动态生成（见 `huawei_iot_build_cmd_resp_topic_from_request()`）。

## 2. 设备上报：door 事件（设备 -> 云）

Topic：`\$oc/devices/<device_id>/user/events/door`

Payload（`key=value`）字段：

- `door`：`open` / `close`
- `method`：`pin` / `rfid` / `fingerprint` / `cloud` / `unknown`
- `ts`：秒级时间戳

示例：

- `door=open,method=rfid,ts=1700000000`
- `door=close,method=cloud,ts=12345`

含义说明：

- `door=open`：发生“开锁动作/门已打开”的业务事件（当前固件用于表示“解锁成功/执行开锁命令”）
- `door=close`：发生“关锁/门已关闭”的业务事件（当前固件用于表示“收到关锁命令”）

## 3. 用户命令：下发（云 -> 设备）

Topic：`\$oc/devices/<device_id>/user/cmd`

通用字段：

- `cmd`：命令名（必填）

### 3.1 `cmd=ping`

作用：链路探活。

- 下发：`cmd=ping`
- ack：`result_code=0,result_desc=pong,ts=<...>`

### 3.2 `cmd=time_sync`

作用：触发一次 SNTP 查询（固件会发送 `AT+CIPSNTPTIME?`）。

- 下发：`cmd=time_sync`
- ack：`result_code=0,result_desc=time_sync_started,ts=<...>`

### 3.3 `cmd=temp`

作用：触发一次温度上报（如果固件/传感器实现启用）。

- 下发：`cmd=temp`
- ack：`result_code=0,result_desc=temp_reported,ts=<...>`

### 3.4 `cmd=unlock`（云端开锁）

作用：云端请求开锁。

固件行为：

1) 将命令投递到执行器队列（`LockActuator_UnlockAsync()`）
2) 上报一次 door 事件：`door=open,method=cloud,...`
3) 返回 ack：`unlock_accepted`

- 下发：`cmd=unlock`
- ack：`result_code=0,result_desc=unlock_accepted,ts=<...>`

执行器动作说明（舵机 MG90S）：

- 舵机 PWM 由 TIM3 CH1（PC6）输出
- 开锁保持时间由 `LOCK_ACTUATOR_UNLOCK_HOLD_MS` 宏控制（默认 5000ms）
- 由执行器任务 `lock_act` 自动回锁（到期 `mg90s_lock()`）

### 3.5 `cmd=lock`（云端关锁）

作用：云端请求关锁（立即回到锁定位置）。

固件行为：

1) 投递执行器命令（`LockActuator_LockAsync()`）
2) 上报一次 door 事件：`door=close,method=cloud,...`
3) 返回 ack：`lock_accepted`

- 下发：`cmd=lock`
- ack：`result_code=0,result_desc=lock_accepted,ts=<...>`

### 3.6 `cmd=door,state=open|close`（注入门事件，联调用）

作用：不驱动硬件，仅注入一次 door 上报，方便联调演示。

- 下发：`cmd=door,state=open`
- ack：`result_code=0,result_desc=door_event_accepted,ts=<...>`

### 3.7 未实现命令

其他命令当前统一返回占位：

- ack：`result_code=1,result_desc=todo,ts=<...>`

## 4. 用户命令：应答（设备 -> 云）

Topic：`\$oc/devices/<device_id>/user/cmd/ack`

Payload 字段：

- `result_code`：`0`=成功，`1`=占位/未实现，`2`=参数缺失
- `result_desc`：结果描述
- `ts`：时间戳（秒）

示例：

- `result_code=0,result_desc=pong,ts=1700000001`
- `result_code=2,result_desc=missing_cmd,ts=1700000002`

## 5. IoTDA 标准命令（sys/commands）补充说明

固件支持 `command_name=unlock` 与 `command_name=lock`：

- 收到 `unlock`：投递执行器开锁 + 上报 door=open(method=cloud) + 发布 JSON response
- 收到 `lock`：投递执行器关锁 + 上报 door=close(method=cloud) + 发布 JSON response

响应 JSON 示例（由固件发送）：

- `{"result_code":0,"response_name":"unlock_response","paras":{}}`
- `{"result_code":0,"response_name":"lock_response","paras":{}}`

