# Flutter + 后端对接指南（以当前固件实现为准）

## 1. 推荐总体方案（两种）

### 方案 A：Flutter 直连 IoTDA（不推荐做“生产级”）

优点：链路短，开发快。  
缺点：设备密钥/鉴权不可下发到 App；权限/审计/多用户难做。

适用：课程/演示。

### 方案 B：后端桥接 IoTDA（推荐）

后端负责：

- 订阅设备事件（door/open/close 等）并落库
- 对 App 暴露 REST/WebSocket API（开锁、查询记录、设备列表、权限管理）
- 代表 App 向 IoTDA 下发 MQTT 命令（unlock/lock/…）

Flutter 负责：

- 登录与用户权限 UI
- 设备列表、门锁状态、开锁按钮
- 开锁记录/告警记录展示

## 2. 与固件对接的“最小可用”能力

固件当前支持（云端）：

- 下发：`cmd=unlock`、`cmd=lock`（user topic）或 IoTDA 标准 `sys/commands`（`command_name=unlock/lock`）
- 上报：door 事件（open/close + method + ts）
- 回包：user topic 的 `cmd/ack`（key=value）与 `sys/commands` 的 JSON response

协议细节见：`docs/mqtt-control.md`

## 3. 后端建议的数据表（示例）

### 3.1 设备表 `devices`

- `id`（主键）
- `iotda_device_id`（字符串，唯一）
- `name`（用户可改）
- `status`（online/offline/unknown）
- `last_seen_at`（时间）
- `created_at` / `updated_at`

### 3.2 门事件表 `door_events`

- `id`
- `iotda_device_id`
- `door_state`（open/close）
- `method`（pin/rfid/fingerprint/cloud/unknown）
- `device_ts`（设备上报的 `ts`）
- `server_ts`（服务器接收时间）
- `raw_payload`（原始字符串）

## 4. 后端 API 建议（示例）

> 注意：固件不提供 HTTP，这些 API 是后端对 App 的接口定义建议。

- `POST /api/devices/{id}/unlock`
- `POST /api/devices/{id}/lock`
- `GET  /api/devices/{id}/events?type=door&limit=100`
- `GET  /api/devices`（列表）

后端实现“下发命令”的方式：

- 通过 IoTDA MQTT publish 到 `.../user/cmd`，payload 为 `cmd=unlock` 或 `cmd=lock`
- 等待 `.../user/cmd/ack` 与 `.../user/events/door` 确认结果

## 5. Flutter 端交互建议（避免“看起来开锁但实际没开”）

建议 UI 采用“三段式状态”：

1) 用户点击开锁：立刻进入 “请求已发送/等待设备响应”
2) 收到 ack：变为 “设备已接受命令（等待执行）”
3) 收到 door=open 事件：变为 “已开锁”，并开始倒计时（与固件保持时间一致）

异常处理：

- 超时（未收到 ack / 未收到事件）：提示用户重试并记录日志
- 设备离线：禁止下发或提示离线

