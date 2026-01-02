# MQTT 控制命令协议（云端交互用）

本协议用于你在云端直接通过 MQTT 与设备交互（不依赖 IoTDA 控制台的“标准命令 JSON”），与当前固件实现保持一致。

## 1. 主题（Topic）

以下 `<device_id>` 为 `Application/Inc/huawei_iot_config.h` 中的 `HUAWEI_IOT_DEVICE_ID`：

- 门事件上报（设备 -> 云）  
  `\$oc/devices/<device_id>/user/events/door`

- 控制命令下发（云 -> 设备）  
  `\$oc/devices/<device_id>/user/cmd`

- 控制命令应答（设备 -> 云）  
  `\$oc/devices/<device_id>/user/cmd/ack`

## 2. 编码与约束

- 字符集：ASCII（推荐只用字母/数字/下划线/逗号/等号）
- 载荷格式：`key=value`，多个字段用英文逗号分隔
- 原因：ESP8266 `AT+MQTTPUB` 以字符串参数发布，包含引号/大括号时容易转义出错；因此当前版本先不用 JSON。

## 3. 门事件上报（设备 -> 云）

Topic：`\$oc/devices/<device_id>/user/events/door`

Payload 字段：

- `door`：`open` / `close`
- `method`：`pin` / `rfid` / `fingerprint` / `cloud` / `unknown`
- `ts`：秒级时间戳（若 SNTP 成功则为 epoch 秒，否则为上电秒）

示例：

- `door=open,method=rfid,ts=1700000000`
- `door=close,method=cloud,ts=12345`

## 4. 控制命令下发（云 -> 设备）

Topic：`\$oc/devices/<device_id>/user/cmd`

通用字段：

- `cmd`：命令名（必填）

### 4.1 `cmd=ping`

用途：连通性探测。

示例：

- 下发：`cmd=ping`

应答（见 5 节）：

- `result_code=0,result_desc=pong,ts=<...>`

### 4.2 `cmd=time_sync`

用途：触发一次 SNTP 查询（AT+CIPSNTPTIME?），用于手动校时/校验链路。

示例：

- 下发：`cmd=time_sync`

应答：

- `result_code=0,result_desc=time_sync_started,ts=<...>`

### 4.3 `cmd=unlock`

用途：云端请求“开锁”（当前为占位动作：固件会记录一次 cloud 开锁事件并上报 door=open）。

示例：

- 下发：`cmd=unlock`

应答：

- `result_code=0,result_desc=unlock_accepted,ts=<...>`

### 4.4 `cmd=door,state=open|close`

用途：云端主动注入一次门状态事件（用于联调/演示）。

示例：

- 下发：`cmd=door,state=open`

应答：

- `result_code=0,result_desc=door_event_accepted,ts=<...>`

### 4.5 未实现命令

其他命令当前统一返回占位：

- `result_code=1,result_desc=todo,ts=<...>`

## 5. 命令应答（设备 -> 云）

Topic：`\$oc/devices/<device_id>/user/cmd/ack`

Payload 字段：

- `result_code`：`0`=成功，`1`=占位/未实现，`2`=参数缺失
- `result_desc`：结果描述字符串
- `ts`：设备当前时间（同 3 节）

示例：

- `result_code=0,result_desc=pong,ts=1700000001`
- `result_code=2,result_desc=missing_cmd,ts=1700000002`

## 6. 代码对应关系（方便你定位）

- 命令解析/执行：`Application/Src/mqtt_at_task.c`
- 门事件投递接口：`Application/Inc/wifi_mqtt_task.h`
- 门事件发布 topic：`Application/Src/mqtt_at_task.c`（调用 `huawei_iot_build_user_door_event_topic()`）

