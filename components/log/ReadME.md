这份文档总结了基于 **CMSIS-RTOS2** 和 **RingBuffer** 实现的企业级日志管理框架 (`log.c` / `log.h`)。

该框架采用 **“双模架构（Dual-Mode Architecture）”**，能够智能感知 RTOS 运行状态，在**系统初始化/异常时使用同步阻塞模式**，在**正常运行时使用异步缓冲模式**，兼顾了调试便利性和系统实时性。

---

### 1. 核心功能特性 (Features)

*   **分级管理**：支持 `ERROR`, `WARN`, `INFO`, `DEBUG` 四个等级，可编译期过滤。
*   **富文本输出**：自动附加 时间戳、日志标签(Tag)、文件名、行号，支持 ANSI 颜色高亮。
*   **双模切换**：
    *   **同步模式 (Sync)**：直接通过串口阻塞发送（适用于 OS 未启动或 HardFault）。
    *   **异步模式 (Async)**：写入 RingBuffer，后台低优先级任务负责发送（适用于业务运行中，不阻塞 CPU）。
*   **RTOS 感知**：自动检测 `osKernelGetState()` 来决定使用哪种模式。
*   **格式化安全**：使用 `vsnprintf` 防止缓冲区溢出。

---

### 2. API 使用规范 (API Reference)

#### 2.1 初始化
必须在 `main.c` 中，硬件初始化之后、调度器启动之前调用。
```c
// 在 main.c 的 USER CODE BEGIN 2 区域
Log_Init(); 
```

#### 2.2 日志打印宏 (推荐方式)
用户**不应**直接调用 `Log_Printf`，而是使用以下宏：

| 宏 | 颜色 | 用途 | 示例 |
| :--- | :--- | :--- | :--- |
| **`LOG_E(tag, fmt, ...)`** | 🔴 红 | 严重错误，模块崩溃 | `LOG_E("WIFI", "Connect Failed: %d", err);` |
| **`LOG_W(tag, fmt, ...)`** | 🟡 黄 | 警告，非致命问题 | `LOG_W("BAT", "Voltage low: %dmV", vol);` |
| **`LOG_I(tag, fmt, ...)`** | 🟢 绿 | 关键状态流转 | `LOG_I("SYS", "System Init OK");` |
| **`LOG_D(tag, fmt, ...)`** | 🔵 蓝 | 调试数据，发布可关 | `LOG_D("SENS", "Raw Data: 0x%02X", data);` |

**参数说明**：
*   `tag`: 字符串标签，用于标识模块（如 "APP", "MOTOR", "4G"），方便在串口助手中过滤。
*   `fmt`: 格式化字符串，用法同 `printf`。
*   `...`: 可变参数。

---

### 3. 配置指南 (`log.h`)

通过修改头文件宏定义来裁剪功能：

```c
// [核心开关] 1: 启用 RingBuffer+Task 异步发送; 0: 强制全部使用阻塞发送(省内存)
#define LOG_ASYNC_ENABLE    1 

// [颜色开关] 1: 启用 ANSI 颜色; 0: 关闭(省 Flash)
#define LOG_COLOR_ENABLE    1   

// [缓冲大小] 定义 RingBuffer 大小 (字节)，决定了瞬间能缓冲多少日志
#define LOG_RB_SIZE         2048 

// [过滤等级] 低于此等级的日志在编译阶段会被优化掉，不占空间
#define LOG_CURRENT_LEVEL   LOG_LEVEL_DEBUG
```

---

### 4. 内部实现逻辑 (Implementation Details)

#### 4.1 智能双模逻辑
函数 `Log_Printf` 内部执行流程：
1.  **格式化**：获取互斥锁 -> `vsnprintf` 格式化内容到 `static log_buf` -> 释放互斥锁。
2.  **模式判断**：检查 `osKernelGetState() == osKernelRunning`。
    *   **是 (OS 运行中)**：
        *   尝试写入 RingBuffer。
        *   写入成功 -> 调用 `osThreadFlagsSet` 唤醒后台 LogTask。
        *   写入失败（满）-> 丢弃（避免阻塞高优先级业务）。
    *   **否 (OS 未启动/中断中/异常)**：
        *   调用 `Hardware_Send` (HAL_UART_Transmit) 直接发送。

#### 4.2 后台任务 (`Log_Task_Entry`)
*   平时处于 `Blocked` 状态（挂起），不占用 CPU 资源。
*   收到信号量（Signal）后唤醒。
*   循环从 RingBuffer 读取数据并发送，直到 Buffer 为空。
*   发送使用低优先级，即便串口慢，也不会卡死主业务逻辑。

---

### 5. 线程安全与健壮性 (Thread Safety)

这是该模块达到“企业级”的核心原因：

1.  **格式化缓冲区的保护**：
    *   使用了 `static char log_buf` 以节省栈空间。
    *   使用了 **递归互斥锁 (`osMutexRecursive`)** 进行保护。
    *   **效果**：当 Task A 正在格式化日志时，Task B 如果也要打印，会被挂起等待，防止 `log_buf` 内容被覆盖或错乱。

2.  **RingBuffer 的并发保护**：
    *   虽然 RingBuffer 本身通常有临界区保护，但在此架构中，由于 `Log_Printf` 整体被 Mutex 保护，因此对 RingBuffer 的写入操作实际上是**串行化**的，这进一步保证了安全性。

3.  **中断安全性 (ISR Safety)**：
    *   `osThreadFlagsSet` 在 CMSIS-RTOS2 (STM32实现) 中是 ISR 安全的。
    *   注意：如果在中断中调用 `LOG_I`，由于 `osMutexAcquire` 不能在中断中使用，代码需要确保 HAL 库的 `HAL_GetTick` 和串口发送函数是重入安全的（标准 HAL 库通常满足基本要求，但建议中断中尽量少打印）。

4.  **死锁预防**：
    *   初始化阶段（OS 未启动）会跳过 Mutex 操作，直接发送，防止在调度器启动前获取锁导致崩溃。
    *   Mutex 获取设置了超时（`osWaitForever` 或特定时间），防止逻辑卡死。

### 总结
这套框架通过 **CMSIS-RTOS2** 实现了标准的生产者-消费者模型，完美解决了嵌入式日志中 **“打印太快卡死 CPU”** 和 **“多任务打印乱码”** 两个痛点。