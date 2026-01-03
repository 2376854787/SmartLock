# 模块指南：USART1 DMA → RingBuffer 数据通道

本模块用于把 USART1 的 DMA-Idle 接收数据写入环形缓冲（RingBuffer），供任务线程消费。

相关路径：

- `Application/Src/Usart1_manage.c`、`Application/Inc/Usart1_manage.h`
- RingBuffer：`components/ring_buffer/*`
- 静态内存池：`components/memory_allocation/*`
- HAL 回调分发：`Core/Src/freertos.c:HAL_UARTEx_RxEventCallback`

## 1. 初始化

入口：`MyUart_Init()`（当前在 `Core/Src/main.c` 的用户区调用）

主要动作：

1) `CreateRingBuffer(&g_rb_uart1, RINGBUFFER_SIZE)` 创建环形缓冲  
2) `HAL_UARTEx_ReceiveToIdle_DMA(&huart1, DmaBuffer, DMA_BUFFER_SIZE)` 启动 DMA-Idle 接收

## 2. 内存来源（不是 FreeRTOS heap）

RingBuffer 的 `buffer` 来自工程自定义静态内存池：

- `CreateRingBuffer()` 内部调用 `static_alloc()`
- 静态池大小 `MEMORY_POND_MAX_SIZE=8192` bytes（见 `components/memory_allocation/MemoryAllocation.h`）

因此：

- RingBuffer 分配失败不一定意味着 FreeRTOS heap 不够
- 需要同时关注 “FreeRTOS heap” 与 “MemoryPond 静态池”

## 3. 回调与数据搬运

回调入口（DMA-Idle）：

- `HAL_UARTEx_RxEventCallback(huart, Size)`（USART1 分支）

搬运逻辑：

- `process_dma_data()` 计算 DMA 写指针位置，把增量数据写入 RingBuffer
- 处理了“回卷”情况（DMA buffer 循环）

## 4. 消费者任务（示例）

`Core/Src/freertos.c` 的 `uartTask` 原本用于消费 `g_rb_uart1`，但当前消费代码已注释，主要保留为示例/占位。

如需启用：

- 在 `uartTask` 中恢复 `RingBuffer_GetUsedSize()` 与 `ReadRingBuffer()` 的逻辑
- 注意不要在高优先级任务里做大量 `printf`（会影响时序）

*** Delete File: docs/developer-guide/modules/sensors-and-network.md
