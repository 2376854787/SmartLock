# 硬件与引脚说明（面向接线/联调）

最终权威配置以 `SmartLock.ioc`（CubeMX）为准；本文件用于快速把“功能 ↔ 引脚 ↔ 代码入口”对上。

## 1. 模块清单（系统视角）

- MCU：STM32F407ZGT6
- 显示：TFT LCD（FSMC 并口，LVGL 驱动）
- 触摸：GT9xxx（软 I2C）
- 指纹：AS608（UART4）
- RFID：MFRC522（SPI1）
- 执行器：MG90S 舵机（TIM3 PWM）
- 联网：ESP-01S（USART3，ESP-AT + MQTT）
- 传感器：光敏（ADC3）、水滴/液体（ADC1 CH1）
- 人机输入：KEY0/KEY1/KEY2（GPIO），WakeUp（PA0-WKUP）
- 指示：LED0/LED1、蜂鸣器、LCD 背光 PWM（TIM12）

## 2. 关键引脚表（固件代码里可直接定位）

> 下表大部分宏来自 `Core/Inc/main.h`；若某项没有宏（比如 PA0-WKUP），以 `SmartLock.ioc` 的 Pinout 为准。

| 功能 | 引脚 | 端口 | 代码/备注 |
|---|---|---|---|
| KEY0/1/2 | `KEY0_Pin`/`KEY1_Pin`/`KEY2_Pin` | `GPIOE` | `Drivers/BSP/Keys/*` |
| 蜂鸣器 | `BEEP_Pin` | `GPIOF` | `Drivers/BSP/Beep/*`（如启用） |
| LED0/LED1 | `LED0_Pin`/`LED1_Pin` | `GPIOF` | `Core/Src/freertos.c` 里翻转 |
| 光敏 ADC | `Light_Sensor_Pin` | `GPIOF` | `Application/Src/Light_Sensor_task.c` |
| 水滴 ADC | `Water_Sensor_Pin` | `GPIOA` | 传感器任务（如启用） |
| LCD 背光 PWM | `LCD_BL_Pin` | `GPIOB` | TIM12 CH2（`Core/Src/tim.c`） |
| RC522 CS | `Rc522_CS_Pin` | `GPIOG` | `Drivers/BSP/rc522/rc522_my.c` |
| RC522 NRT/IRQ | `Rc522_NRT_Pin` | `GPIOG` | 以硬件接法为准 |
| AS608 UART4 TX/RX | `as608_tx_Pin` / `as608_rx_Pin` | `GPIOC` | UART4：PC10(TX)/PC11(RX)，`Drivers/BSP/as608/*` |
| ESP-01S USART3 TX/RX | PB10 / PB11 | `GPIOB` | USART3：`Application/Src/mqtt_at_task.c` |
| 日志/调试 USART1 TX/RX | PA9 / PA10 | `GPIOA` | USART1 + DMA：`Application/Src/Usart1_manage.c`、log backend |
| 舵机 PWM | PC6 | `GPIOC` | TIM3 CH1：`Drivers/BSP/mg90s/mg90s.c` |
| WakeUp | PA0-WKUP | `GPIOA` | CubeMX 配为 `SYS_WKUP`（不再是普通 GPIO 输入宏） |

## 3. 触摸（GT9xxx）软 I2C 引脚

触摸使用软件 I2C（不是 CubeMX I2C 外设），定义在 `Drivers/BSP/touch/ctiic.h`：

- SCL：PB0（`CT_IIC_SCL_GPIO_PORT=GPIOB`, `CT_IIC_SCL_GPIO_PIN=GPIO_PIN_0`）
- SDA：PF11（`CT_IIC_SDA_GPIO_PORT=GPIOF`, `CT_IIC_SDA_GPIO_PIN=GPIO_PIN_11`）

联调注意：

- 必须有上拉（通常模块自带 4.7k/10k，上拉不足会导致 ACK 超时）
- SDA 被拉死时会出现“初始化卡死/无响应”

## 4. 舵机（MG90S）PWM 参数

TIM3 配置（见 `Core/Src/tim.c`）：

- 计数频率：84MHz / 84 = 1MHz（1 tick = 1us）
- 周期：`20000-1`（约 20ms）
- CH1 初始 `Pulse = 1500us`

舵机角度（固件默认）：

- `mg90s_lock()`：`500us`（约 0°）
- `mg90s_unlock()`：`1500us`（约 90°）

执行器保持时间：

- `LOCK_ACTUATOR_UNLOCK_HOLD_MS`（默认 5000ms，见 `Application/Inc/lock_actuator.h`）

## 5. 供电与电气建议（很重要）

- MG90S 建议独立 5V 供电（电流突发大），与 MCU 必须共地
- RC522/触摸/AS608 多为 3.3V 逻辑，注意电平匹配
- 若出现“舵机一动 MCU 重启/卡死”，优先排查供电压降与地线回路

