# 论文写作提纲（与本工程文档对齐）

> 这是结构建议，方便你把论文每一章对应到“代码实现 + 文档说明 + 实验验证”。  
> 你可以把本文件作为论文目录初稿，然后逐章补充图表与数据。

## 第 1 章 绪论

- 研究背景：智能门锁、人机交互、物联网接入
- 研究目标：本地开锁（指纹/刷卡/PIN）+ 云端控制 + UI 显示与交互
- 研究内容与创新点（建议突出）：
  - LVGL 单线程 UI + worker 解耦（避免 UI 卡死）
  - 执行器任务/队列（避免 UI/云端并发直驱硬件）
  - IoTDA 双通道命令（user topic + sys/commands）与事件上报闭环
- 论文结构

## 第 2 章 系统总体设计

建议引用：

- `docs/system-architecture.md`
- `docs/README.md`（文档索引）

可写内容：

- 功能需求与非功能需求（实时性、稳定性、可维护性）
- 分层架构（Core/Drivers/components/Application）
- 数据流与并发模型（任务图/消息队列图）

## 第 3 章 硬件设计

建议引用：

- `SmartLock.ioc`（CubeMX 引脚配置）
- `docs/hardware.md`

可写内容：

- 各外设选型与接口（UART/SPI/PWM/FSMC/ADC）
- 供电设计与可靠性（舵机供电、共地、滤波）
- 关键参数（PWM 周期/脉宽、串口波特率等）

## 第 4 章 固件软件设计与实现

建议引用：

- `docs/developer-guide/README.md`
- `docs/developer-guide/modules/core-startup-rtos.md`
- `docs/developer-guide/modules/rtos-task-mem-inventory.md`

可写内容：

- 启动流程、RTOS 参数、任务栈/堆配置与验证
- LVGL 集成与线程模型：`docs/developer-guide/modules/lvgl-integration.md`
- UI 业务与 worker：`docs/developer-guide/modules/ui-lock.md`
- 指纹模块：`docs/developer-guide/modules/drivers-as608.md`
- RC522 模块：`docs/developer-guide/modules/drivers-rc522.md`
- 执行器队列任务（舵机）：结合 `docs/change-notes.md` 说明迭代原因
- 业务数据层（RFID/PIN）：`docs/developer-guide/modules/lock-data.md`

## 第 5 章 云端通信与协议设计

建议引用：

- `docs/mqtt-control.md`
- `docs/developer-guide/modules/cloud-huawei-iotda.md`

可写内容：

- topic 设计、字段设计、ACK/response 机制
- 鉴权与时间戳（SNTP、IoTDA 签名）
- 命令下发与事件上报闭环（时序图）

## 第 6 章 Flutter 前端与后端设计（扩展）

建议引用：

- `docs/flutter-backend-guide.md`

可写内容：

- 后端桥接 IoTDA 的必要性（多用户/权限/审计/密钥保护）
- 数据库表设计（devices、door_events）
- Flutter UI 状态机（请求发送→ack→事件确认）

## 第 7 章 测试与结果分析

建议从“可复现”角度写：

- 本地开锁链路：指纹/刷卡/PIN 的验证成功率与 UI 响应
- 云端开锁链路：命令下发、ack、door event 的时延统计
- 稳定性：长时间运行、堆/栈水位、触摸稳定性
- 供电抗扰：舵机动作导致的压降/复位问题与解决

## 第 8 章 总结与展望

- 总结：完成的功能与关键设计点
- 展望：
  - IoTDA 标准属性上报（JSON）
  - 权限体系与审计
  - 固件 OTA 与安全加固

