# 硬件与引脚说明（面向联调/接线）

本文给出本工程涉及的主要外设模块与“能快速定位到的引脚入口”。**最终以 `SmartLock.ioc`（CubeMX）为准**，本文用于帮助你在联调阶段快速对上“功能 ↔ 引脚 ↔ 代码”。

## 1. 外设模块清单（工程视角）

- 显示与触摸：LCD + 触摸面板（LVGL 驱动链路见 `docs/developer-guide/modules/lvgl-integration.md`）
- 本地开锁：
  - 指纹：AS608（UART）
  - RFID：RC522（SPI）
  - 密码：触摸 UI 输入（PIN 存储由 `lock_data` 管理）
- 联网：ESP-01S（ESP8266 AT，UART3）+ MQTT
- 传感器：光敏（ADC3）、水滴/液体（ADC1 CH1）
- 声光：蜂鸣器、LED、LCD 背光
- 按键：KEY0/KEY1/KEY2 + wake_up

## 2. 引脚速查（来自 `Core/Inc/main.h`）

以下表格的引脚宏由 CubeMX 生成到 `Core/Inc/main.h`，用于在代码里统一引用。

| 功能 | 引脚宏 | 说明 |
|---|---|---|
| KEY0/1/2 | `KEY0_Pin` / `KEY1_Pin` / `KEY2_Pin` | 三个按键输入 |
| wake_up | `wake_up_Pin` | 唤醒键 |
| 蜂鸣器 | `BEEP_Pin` | 蜂鸣器控制 |
| LED0/LED1 | `LED0_Pin` / `LED1_Pin` | 指示灯 |
| LCD 背光 | `LCD_BL_Pin` | 背光控制 |
| 光敏 | `Light_Sensor_Pin` | ADC3 采样输入 |
| 水滴/液体 | `Water_Sensor_Pin` | ADC1 CH1 采样输入 |
| RC522 片选 | `Rc522_CS_Pin` | SPI 片选 |
| RC522 中断 | `Rc522_NRT_Pin` | RC522 NRT/IRQ（以硬件接法为准） |
| AS608 TX/RX | `as608_tx_Pin` / `as608_rx_Pin` | AS608 UART（以 `SmartLock.ioc` 的 UART 配置为准） |

## 3. “没在表里”的引脚怎么查？

LCD 并行总线（FSMC）、触摸 I2C、SPI SCK/MISO/MOSI、ESP-01S 的 UART3 等引脚通常不会全部以“功能名宏”列在同一处。推荐按以下路径定位：

1. 打开 `SmartLock.ioc`（CubeMX）查看 Pinout & Configuration（最权威）
2. 查看 `Core/Inc/*.h` 中的外设句柄声明与初始化（如 `Core/Inc/usart.h`、`Core/Inc/spi.h`、`Core/Inc/fsmc.h`）
3. 再回到对应驱动/任务入口（例如 `Application/Src/mqtt_at_task.c`、`Drivers/BSP/rc522/rc522_my.c`）

