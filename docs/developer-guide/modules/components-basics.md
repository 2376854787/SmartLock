# 模块指南：基础组件（time / memory / osal）

本工程除了 HAL/FreeRTOS 之外，还引入了一些“基础设施组件”，用于提升可移植性与可维护性：

- `hal_time`：统一的毫秒/微秒时间戳接口（头文件在 `components/hal/include/hal_time.h`，STM32 端口在 `platform/STM32/ports/hal_time_port.c`）
- `components/memory_allocation`：静态内存池（给 RingBuffer/Log/AT 用）
- `components/osal`：OS 抽象层（把线程/互斥/信号/时间等抽象出来）

## 1. 时间（us/ms）与 DWT

### 1.1 `hal_time`（平台抽象）

入口：

- 接口：`components/hal/include/hal_time.h`
- STM32 端口实现：`platform/STM32/ports/hal_time_port.c`

目标：

- 提供 `ms/us` 时间戳，给超时控制、性能测量、协议等待使用

实现策略（推荐）：

- 优先使用 DWT 周期计数器做 us 级时间
- DWT 不可用时降级为 `HAL_GetTick()*1000`

### 1.2 `delay_us()`（驱动常用）

许多 BSP 驱动（触摸软 I2C 等）使用 `delay_us()`：

- `Drivers/System/Inc/My_delay.h`
- `Drivers/System/Src/My_delay.c`

工程经验：

- 若 `delay_us()` 依赖 SysTick/RTOS tick，而在调度器未启动时调用，可能造成“卡死无日志”
- 更稳的方式是：优先用 DWT 做 busy-wait，必要时再降级

## 2. 静态内存池（MemoryPond）

位置：

- `components/memory_allocation/MemoryAllocation.h`
- `components/memory_allocation/MemoryAllocation.c`

特点：

- 单一大数组线性分配（`static_alloc()`）
- 不支持释放（reset 只能整池清空）
- 适合“启动阶段一次性分配”的对象（RingBuffer）

当前主要使用者：

- RingBuffer（USART1 DMA、Log、AT）

## 3. OSAL（操作系统抽象层）

位置：

- `components/osal/*`

作用：

- 把 CMSIS-RTOS2 的线程/互斥/信号量/时间等封装为统一 API
- 让组件代码（log/AT）不直接依赖 FreeRTOS API

注意：

- OSAL 会判断 “内核是否运行”，在 scheduler 未启动时避免使用阻塞能力
- 这也是你在日志里看到“RTOS 调度器没启动”的根本原因之一
