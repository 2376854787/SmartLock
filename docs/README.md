# SmartClock / SmartLock 文档索引

本文档目录面向两类读者：

1) “能跑起来并联调”的同学：先看 `docs/getting-started.md`、`docs/hardware.md`、`docs/mqtt-control.md`  
2) “要改固件/写论文/做 Flutter+后端”的同学：重点看 `docs/system-architecture.md` 与 `docs/developer-guide/README.md`

## 1. 快速入口（最常用）

- 构建/烧录/常见问题：`docs/getting-started.md`
- 硬件与引脚说明（以 `SmartLock.ioc` 为准）：`docs/hardware.md`
- 云端 MQTT 控制协议（Flutter/后端直接用）：`docs/mqtt-control.md`

## 2. 系统设计（论文/后端/前端依据）

- 系统总体架构与数据流：`docs/system-architecture.md`
- Flutter 与后端落地建议（IoTDA/MQTT）：`docs/flutter-backend-guide.md`
- 变更记录（设计迭代对照）：`docs/change-notes.md`
- 论文提纲（目录初稿）：`docs/thesis-outline.md`

## 3. 固件开发者指南（按模块拆解）

入口：

- `docs/developer-guide/README.md`

模块文档：

- `docs/developer-guide/modules/core-startup-rtos.md`
- `docs/developer-guide/modules/lvgl-integration.md`
- `docs/developer-guide/modules/ui-lock.md`
- `docs/developer-guide/modules/lock-data.md`
- `docs/developer-guide/modules/drivers-as608.md`
- `docs/developer-guide/modules/drivers-rc522.md`
- `docs/developer-guide/modules/cloud-huawei-iotda.md`
- `docs/developer-guide/modules/components-log.md`
- `docs/developer-guide/modules/components-at.md`
- `docs/developer-guide/modules/uart1-dma-ringbuffer.md`
- `docs/developer-guide/modules/sensors-and-network.md`
- RTOS/内存/初始化清单（CubeMX 重生成前对照）：`docs/developer-guide/modules/rtos-task-mem-inventory.md`
