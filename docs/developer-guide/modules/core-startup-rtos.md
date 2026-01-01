# 模块指南：系统入口与 RTOS（Core）

## 模块职责

- 负责 **启动链路**：时钟、外设初始化、RTOS 启动。
- 负责 **任务创建**：Key 扫描、UART 消费、LVGL、（可选）传感器/联网任务。
- 负责 **HAL 回调转发**：把“中断上下文事件”转发给 AT/Log/AS608/USART1‑DMA 等模块。

相关路径：
- `Core/Src/main.c`
- `Core/Src/freertos.c`
- `Core/Src/stm32f4xx_it.c`

## 核心流程（启动→任务）

```mermaid
flowchart TD
  A[main()] --> B[HAL_Init + SystemClock_Config]
  B --> C[MX_* 外设初始化]
  C --> D[用户初始化<br/>lcd_init / MyUart_Init / KEY_Init]
  D --> E[osKernelInitialize]
  E --> F[MX_FREERTOS_Init]
  F --> G[创建线程<br/>KeyScanTask / uartTask / LightSensor_Task ...]
  G --> H[初始化服务<br/>Log_PortInit+Log_Init<br/>at_core_task_init<br/>lvgl_init]
  H --> I[osKernelStart]
```

## 任务与回调关系（事件驱动）

```mermaid
flowchart TD
  subgraph IRQ[中断/回调（HAL/CMSIS）]
    I1[TIM6 IRQ<br/>KEY_Tick_Handler()]
    I2[HAL_UARTEx_RxEventCallback]
    I3[HAL_UART_TxCpltCallback]
    I4[HAL_UART_RxCpltCallback]
    I5[HAL_UART_ErrorCallback]
  end

  subgraph Tasks[线程（FreeRTOS）]
    T1[KeyScanTask<br/>周期 KEY_Tasks()]
    T2[uartTask<br/>读 g_rb_uart1]
    T3[AT_Core_Task<br/>解析/发送 AT]
    T4[lvgl_handler_task<br/>lv_timer_handler+UI]
    T5[lvgl_tick_task<br/>lv_tick_inc]
  end

  I1 --> T1
  I2 -->|USART1| T2
  I2 -->|USART3| T3
  I3 -->|DMA TX done| T3
  I3 -->|DMA TX done| TLog[LogTask（内部）]
  I4 -->|UART4 RX byte| TAs608[AS608 Service（内部）]
  I5 -->|USART1 error| T2
```

> 说明：Log/AS608 的“内部任务/状态机”由其模块自行创建与驱动；Core 只负责 **回调转发与初始化顺序**。

## Public API 速查表（本模块）

> Core 层对外基本不提供“可复用库式 API”，主要入口由 CubeMX/HAL 固定生成；开发者通常只需要知道“在哪里挂任务/回调”。

| 函数名 | 作用 | 关键参数 | 备注 |
|---|---|---|---|
| `MX_FREERTOS_Init()` | 创建 CMSIS‑RTOS2 线程与初始化服务 | 无 | 任务创建入口，业务扩展优先从这里加 |
| `HAL_UARTEx_RxEventCallback()` | 串口 DMA‑Idle 收包回调分发 | `huart`, `Size` | USART1→`process_dma_data()`；USART3→`AT_Core_RxCallback()` |
| `HAL_UART_TxCpltCallback()` | 串口 DMA 发送完成通知 | `huart` | 转发给 `AT_Manage_TxCpltCallback()` 与 `LOG_UART_TxCpltCallback()` |
| `HAL_UART_RxCpltCallback()` | 串口 1 字节接收完成（AS608） | `huart` | 转发给 `AS608_Port_OnUartRxCplt()` |
| `HAL_UART_ErrorCallback()` | 串口错误处理 | `huart` | USART1 发生错误时重启 DMA；并转发 `AS608_Port_OnUartError()` |

## 关键参数（物理含义）

| 配置项 | 位置 | 含义 |
|---|---|---|
| `KEY0_Pin/KEY1_Pin/KEY2_Pin` 等 | `Core/Inc/main.h` | 按键/LED/外设片选等硬件引脚定义（来自 CubeMX） |
| `configTICK_RATE_HZ` | `Core/Inc/FreeRTOSConfig.h` | FreeRTOS tick 频率；影响 `lock_time_now_s()` 等“秒”换算 |

## Design Notes（为什么这么写）

- **回调集中转发**：避免多个文件同时定义 `HAL_UART_*Callback` 导致冲突；同时把“硬件事件”统一分流到各模块，降低耦合。
- **Key 的两段式驱动**：把计时（TIM6 IRQ）与逻辑（任务）分离，保证中断短小，同时让“多击/长按”这种状态机更容易维护。
- **LVGL 单线程约束**：LVGL 对并发不友好，采用专用 handler task + `lv_async_call` 回到 UI 线程，避免随机崩溃/卡死。
