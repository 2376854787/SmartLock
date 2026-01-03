# 变更记录（便于写论文/对照联调）

本文件记录“工程结构/关键配置”的重要变化点，方便你在论文里解释设计迭代，也方便后续用 CubeMX 重生成时做回归核对。

## 1) 执行器：新增“任务/队列”模型（避免 UI 直驱硬件）

- 新增执行器模块：`Application/Src/lock_actuator.c`、`Application/Inc/lock_actuator.h`
- 设计要点：
  - UI/云端只投递 `LockActuator_UnlockAsync()` / `LockActuator_LockAsync()`
  - `lock_act` 任务串行执行 `mg90s_unlock()`/`mg90s_lock()`，并根据 `LOCK_ACTUATOR_UNLOCK_HOLD_MS` 自动回锁

## 2) 外设初始化：AS608/RC522 初始化集中到 `dev_init`

- 新增设备初始化模块：`Application/Src/lock_devices.c`、`Application/Inc/lock_devices.h`
- 设计要点：
  - 在 RTOS 启动后初始化 AS608 service 与 RC522
  - 通过 EventGroup 设置 ready bits，UI/worker 使用 `LockDevices_WaitAs608Ready()` / `LockDevices_WaitRc522Ready()` 等待

## 3) 云端控制：unlock/lock 命令实际驱动舵机

- 命令处理入口：`Application/Src/mqtt_at_task.c`
- 支持：
  - user topic：`cmd=unlock` / `cmd=lock`（见 `docs/mqtt-control.md`）
  - IoTDA `sys/commands`：`command_name=unlock` / `command_name=lock`

## 4) CubeMX 配置变化（可见于 git diff 与 SmartLock.ioc）

- TIM3 PWM（舵机）加入 CubeMX 并在 `main()` 调用 `MX_TIM3_Init()`：
  - PWM 引脚：PC6（TIM3_CH1）
  - 配置：20ms 周期，1us 分辨率（Prescaler=84-1）
- FreeRTOS（CubeMX）线程改为静态分配：
  - `KeyScanTask` / `uartTask` / `lcdTask` 使用 `cb_mem`/`stack_mem`
- `wake_up` 从普通 GPIO 输入改为 `SYS_WKUP`（PA0-WKUP），对应宏从 `Core/Inc/main.h` 中移除
- TIM12 背光 PWM 参数更新（`Core/Src/tim.c`）

## 5) 对照文档

- RTOS/内存/初始化清单：`docs/developer-guide/modules/rtos-task-mem-inventory.md`
- 系统总体架构：`docs/system-architecture.md`
- MQTT 协议：`docs/mqtt-control.md`

*** Add File: docs/thesis-outline.md
# 论文写作提纲（与本工程文档对齐）

> 这是“结构建议”，不是固定模板。你可以直接把每节对应到本仓库的文档与代码位置。

## 第 1 章 绪论

- 研究背景：智能门锁、人机交互、物联网接入
- 研究内容与目标：本地开锁（指纹/刷卡/PIN）+ 云端控制 + UI
- 论文结构说明

## 第 2 章 系统总体设计

建议引用：

- `docs/system-architecture.md`（分层、数据流、并发模型）
- `docs/hardware.md`（硬件模块清单与引脚）

可写内容：

- 系统功能需求与约束
- 硬件架构（MCU + 外设）
- 软件架构（Core/Drivers/components/Application 分层）
- RTOS 并发模型与线程安全原则

## 第 3 章 硬件设计与实现

建议引用：

- `SmartLock.ioc`（CubeMX 引脚配置）
- `docs/hardware.md`（关键引脚与电气建议）

可写内容：

- LCD/FSMC、触摸软 I2C、UART/SPI、PWM 舵机
- 供电与可靠性（舵机独立供电、共地等）

## 第 4 章 软件设计与实现（固件）

建议引用：

- `docs/developer-guide/README.md`
- `docs/developer-guide/modules/*`

可写内容：

- 启动链路与 RTOS 任务（`core-startup-rtos` + 任务栈/堆参数）
- LVGL 线程模型与 UI 解耦（`lvgl-integration` + `ui-lock`）
- 指纹/RC522 驱动与服务封装（`drivers-as608`、`drivers-rc522`）
- 执行器任务/队列设计（`lock_actuator`，可配合 `docs/change-notes.md` 说明“为什么要这样改”）
- 数据层（RFID/PIN）：`lock-data`

## 第 5 章 云端通信与协议设计

建议引用：

- `docs/mqtt-control.md`
- `docs/developer-guide/modules/cloud-huawei-iotda.md`

可写内容：

- 主题设计、消息字段、ACK/response 机制
- 安全性与鉴权（IoTDA 设备密钥、时间戳）
- 事件上报与命令下发流程图

## 第 6 章 Flutter 前端与后端设计（可选/扩展）

建议引用：

- `docs/flutter-backend-guide.md`

可写内容：

- 后端桥接 IoTDA 的必要性（多用户、权限、审计）
- 数据库表设计（devices/door_events）
- Flutter UI 状态机（请求发送/ack/事件确认）

## 第 7 章 测试与结果分析

建议结合：

- 功能测试：本地开锁、云端开锁、事件上报完整链路
- 性能/稳定性：堆/栈水位、UI 响应、触摸稳定性
- 可靠性：电源扰动（舵机负载）、异常恢复策略

## 第 8 章 总结与展望

- 总结：完成的功能与关键设计点
- 展望：标准 IoTDA 属性上报、权限体系、固件 OTA、日志/诊断增强等

