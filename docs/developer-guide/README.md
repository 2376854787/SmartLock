# SmartClock（SmartLock）开发者指南（固件侧）

本文档是“读代码的导航图”，目标是让你在不翻全仓库的情况下快速理解：

- 上电到 RTOS 的启动链路
- LVGL 的线程模型（UI 为什么不能直接驱动硬件）
- 指纹/RC522/舵机/云端任务的边界与调用关系
- 后续用 CubeMX 重生成初始化时，哪些“自写初始化调用点”需要断开

如果你是为了写论文/做 Flutter+后端，请同时阅读：

- 系统总体架构：`docs/system-architecture.md`
- MQTT 对接协议：`docs/mqtt-control.md`

## 1. 目录分层（从“职责”理解）

- `Core/`：启动入口、CubeMX 生成代码、RTOS 初始化入口、HAL 回调分发
- `Application/`：业务与 UI（门锁界面、worker、云端 MQTT 任务、执行器队列）
- `Drivers/BSP/`：外设驱动（LCD/触摸/AS608/RC522/MG90S/按键等）
- `components/`：可复用组件（log/AT/OSAL/ring_buffer/静态内存池/HFSM 等）
- `platform/`：平台抽象（`hal_time` 等）

## 2. 启动链路（Top-Down）

入口文件：

- `Core/Src/main.c`：`HAL_Init()` → `SystemClock_Config()` → `MX_*_Init()` → `osKernelInitialize()` → `MX_FREERTOS_Init()` → `osKernelStart()`
- `Core/Src/freertos.c`：创建 CMSIS-RTOS2 线程 + 初始化各服务模块（Log/AT/LVGL/设备初始化/执行器）

关键补充：

- TIM3（MG90S 舵机 PWM）由 CubeMX 生成并在 `main()` 中 `MX_TIM3_Init()`（见 `Core/Src/main.c`）
- 部分日志会提示“RTOS 调度器没启动”：这是 `osKernelStart()` 之前的正常现象，详见 `docs/getting-started.md`

## 3. 并发模型（必须遵守的规则）

1) **LVGL 单线程规则**：只有 `lvgl_handler` 线程允许直接调用 LVGL API  
2) **硬件访问解耦**：UI 线程不直接驱动 AS608/RC522/MG90S，通过 worker/queue/task 发送命令  
3) **外设初始化在 RTOS 后做**：指纹/RC522 初始化统一在 `dev_init` 任务中做，避免“时基未就绪导致卡死”

## 4. 文档索引（按模块）

- 系统入口与 RTOS：`docs/developer-guide/modules/core-startup-rtos.md`
- LVGL 集成与显示/触摸：`docs/developer-guide/modules/lvgl-integration.md`
- 主 UI（多模式门锁）：`docs/developer-guide/modules/ui-lock.md`
- 门锁业务数据（RFID/PIN 存储）：`docs/developer-guide/modules/lock-data.md`
- 指纹 AS608（端口+服务封装）：`docs/developer-guide/modules/drivers-as608.md`
- RFID RC522：`docs/developer-guide/modules/drivers-rc522.md`
- 传感器与联网任务：`docs/developer-guide/modules/sensors-and-network.md`
- USART1 DMA→RingBuffer：`docs/developer-guide/modules/uart1-dma-ringbuffer.md`
- 日志系统：`docs/developer-guide/modules/components-log.md`
- AT 框架（USART3）：`docs/developer-guide/modules/components-at.md`
- IoTDA（ESP-01S + MQTT）：`docs/developer-guide/modules/cloud-huawei-iotda.md`
- RTOS/内存/初始化清单（CubeMX 重生成前对照）：`docs/developer-guide/modules/rtos-task-mem-inventory.md`

## 5. CubeMX 重生成前的约定

你后续计划用 CubeMX 统一生成初始化代码时，建议：

- 保留现有“自写 init”实现文件（便于参考/迁移）
- 但在本工程主链路中不调用它们（避免双初始化/顺序混乱）
- 具体“当前有哪些非 CubeMX 初始化调用点”已记录在：`docs/developer-guide/modules/rtos-task-mem-inventory.md`

*** Delete File: docs/developer-guide/modules/core-startup-rtos.md
