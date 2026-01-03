# 模块指南：AT 框架（components/AT，USART3）

本模块把 ESP-01S（ESP8266）的 ESP-AT 固件封装为可在 RTOS 中稳定使用的“AT 会话/命令队列”：

- AT Core 线程：负责收包解析、命令状态机、发送调度
- 上层业务（MQTT 任务）：通过 AT Core 提供的接口发送 AT 命令并等待结果

相关路径：

- `components/AT/AT.c`、`components/AT/AT.h`
- `components/AT/AT_Core_Task.c`
- OSAL：`components/osal/*`（内部映射到 CMSIS-RTOS2）
- 云端任务：`Application/Src/mqtt_at_task.c`

## 1. 串口与 DMA

- UART：USART3（PB10=TX / PB11=RX，见 `SmartLock.ioc`）
- DMA：USART3 RX/TX 均配置 DMA（见 `SmartLock.ioc` 的 DMA 配置）
- 工程回调分发：
  - `HAL_UARTEx_RxEventCallback`（USART3）→ `AT_Core_RxCallback`
  - `HAL_UART_TxCpltCallback`（USART3）→ `AT_Manage_TxCpltCallback`

## 2. 线程创建与启动链路

调用入口：

- `Core/Src/freertos.c` 中调用 `at_core_task_init(&g_at_manager, &huart3)`

创建线程：

- `AT_Core_Task`：在 `components/AT/AT_Core_Task.c:at_core_task_init()` 创建
- `MQTT_AT`：AT core 就绪后创建（同文件内创建 `StartMqttAtTask`）

线程栈大小（bytes）见：

- `components/AT/AT_Core_Task.c:87`（AT core）
- `components/AT/AT_Core_Task.c:107`（MQTT_AT）

## 3. AT 命令与超时模型

原则：

- 发送必须有超时（避免 UART 异常导致“整机卡死”）
- 超时到期后释放等待者并继续调度下一条命令

## 4. 与 IoTDA/MQTT 的关系

MQTT 业务在 `Application/Src/mqtt_at_task.c`，它通过 AT core：

- 联网（`esp01s_Init`）
- SNTP 校时（`AT+CIPSNTPCFG` / `AT+CIPSNTPTIME?`）
- MQTT connect/sub/pub（`AT+MQTT*`）

对外协议请看：`docs/mqtt-control.md`

*** Delete File: docs/developer-guide/modules/uart1-dma-ringbuffer.md
