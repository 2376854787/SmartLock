# 模块指南：USART1 DMA→RingBuffer 数据通道

## 模块职责

- USART1 采用 **Receive‑to‑Idle DMA** 接收数据，避免逐字节中断带来的高 CPU 占用。
- DMA 缓冲区的数据被搬运到 **环形缓冲区** `g_rb_uart1`，由任务线程消费（打印/转发/协议解析）。

相关路径：
- `Application/Inc/Usart1_manage.h`
- `Application/Src/Usart1_manage.c`
- 回调分发：`Core/Src/freertos.c`（`HAL_UARTEx_RxEventCallback`）
- 环形缓冲：`components/ring_buffer/*`

## 核心流程

```mermaid
flowchart TD
  A[MyUart_Init] --> B[CreateRingBuffer(g_rb_uart1)]
  B --> C[HAL_UARTEx_ReceiveToIdle_DMA<br/>(huart1, DmaBuffer)]

  C --> D[DMA/IDLE 触发回调<br/>HAL_UARTEx_RxEventCallback]
  D --> E[process_dma_data()]
  E --> F[WriteRingBuffer(g_rb_uart1)]
  F --> G[uartTask 周期读取<br/>ReadRingBuffer]
```

## Public API 速查表

| 函数名 | 作用 | 关键参数 | 备注 |
|---|---|---|---|
| `MyUart_Init()` | 初始化 USART1 DMA 接收与环形缓冲 | 无 | 成功后启动 `ReceiveToIdle_DMA` |
| `process_dma_data()` | 把 DMA 新增数据搬运到环形缓冲 | 无 | 由 `HAL_UARTEx_RxEventCallback` 调用 |
| `g_rb_uart1` | USART1 环形缓冲实例 | - | `uartTask` 读取它做后续处理 |
| `DmaBuffer[]` | USART1 DMA 原始缓冲 | - | 长度由 `DMA_BUFFER_SIZE` 控制 |

## 关键参数（物理含义）

| 配置项 | 位置 | 含义/影响 |
|---|---|---|
| `DMA_BUFFER_SIZE` | `Application/Inc/Usart1_manage.h` | DMA 循环缓冲区大小（字节）；越大越不易溢出但占 RAM |
| `RINGBUFFER_SIZE` | `Application/Inc/Usart1_manage.h` | 软件环形缓冲大小（字节）；应覆盖“消费不及时”时的峰值 |

## Design Notes（为什么这么写）

- **Receive‑to‑Idle DMA**：硬件 DMA 连续搬运，IDLE/半满/满中断只负责“指针推进”，显著降低 CPU 占用并提升突发吞吐。
- **双缓冲语义**：DMA 缓冲是“硬件写入区”，RingBuffer 是“软件消费队列”，二者解耦可以避免任务抖动导致的数据丢失。
- **错误恢复**：USART1 出错时重启 DMA（在 `HAL_UART_ErrorCallback`），防止 ORE/FE 导致接收停滞。

