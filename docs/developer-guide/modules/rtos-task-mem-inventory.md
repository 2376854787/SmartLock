# 模块指南：RTOS 任务/内存/初始化清单（用于 CubeMX 重生成前对照）

> 目标：在你用 CubeMX 统一生成初始化代码之前，先把当前工程的 **RTOS 关键参数**、**任务栈大小**、**堆/静态内存池**、以及 **非 CubeMX 初始化调用点** 记录下来，方便重构/回归排查。

## 1) FreeRTOS 关键参数（编译期）

来源：`Core/Inc/FreeRTOSConfig.h`

| 配置项 | 当前值 | 备注 |
|---|---:|---|
| `configUSE_PREEMPTION` | 1 | 抢占式调度 |
| `configSUPPORT_STATIC_ALLOCATION` | 1 | 支持静态创建 task/queue（见 `Application/Src/lock_actuator.c`） |
| `configSUPPORT_DYNAMIC_ALLOCATION` | 1 | 支持动态创建 task/queue |
| `configTICK_RATE_HZ` | 1000 | 1ms tick（`Core/Inc/FreeRTOSConfig.h:72`） |
| `configMAX_PRIORITIES` | 56 | `Core/Inc/FreeRTOSConfig.h:73` |
| `configMINIMAL_STACK_SIZE` | 128 | **word**（`Core/Inc/FreeRTOSConfig.h:74`） |
| `configTOTAL_HEAP_SIZE` | 36864 | FreeRTOS heap_4（`Core/Inc/FreeRTOSConfig.h:76`） |
| `configCHECK_FOR_STACK_OVERFLOW` | 2 | 开启溢出检测（`Core/Inc/FreeRTOSConfig.h:83`） |
| `configUSE_MALLOC_FAILED_HOOK` | 1 | 开启 malloc fail hook（`Core/Inc/FreeRTOSConfig.h:85`） |
| `configUSE_TIMERS` | 1 | 软件定时器服务任务开启（`Core/Inc/FreeRTOSConfig.h:101`） |
| `configTIMER_TASK_PRIORITY` | 2 | `Core/Inc/FreeRTOSConfig.h:102` |
| `configTIMER_QUEUE_LENGTH` | 10 | `Core/Inc/FreeRTOSConfig.h:103` |
| `configTIMER_TASK_STACK_DEPTH` | 256 | **word**（`Core/Inc/FreeRTOSConfig.h:104`） |
| `configGENERATE_RUN_TIME_STATS` | 1 | 运行时统计开启（`Core/Inc/FreeRTOSConfig.h:78`）；计数器接口在 `Core/Src/freertos.c:176`（弱符号） |
| `configUSE_TRACE_FACILITY` | 1 | `Core/Inc/FreeRTOSConfig.h:79` |
| `configASSERT(x)` | 死循环 | `Core/Inc/FreeRTOSConfig.h:164` |
| `USE_FreeRTOS_HEAP_4` | defined | `Core/Inc/FreeRTOSConfig.h:134` |

中断优先级相关（CubeMX/FreeRTOS 关键项，便于以后排查 “ISR 调用 RTOS API 卡死/HardFault”）：

- `Core/Inc/FreeRTOSConfig.h:146`：`configLIBRARY_LOWEST_INTERRUPT_PRIORITY = 15`
- `Core/Inc/FreeRTOSConfig.h:152`：`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5`
- `Core/Inc/FreeRTOSConfig.h:156`：`configKERNEL_INTERRUPT_PRIORITY = (15 << (8 - configPRIO_BITS))`
- `Core/Inc/FreeRTOSConfig.h:159`：`configMAX_SYSCALL_INTERRUPT_PRIORITY = (5 << (8 - configPRIO_BITS))`

## 2) 链接脚本：C Heap/主栈保底（非 FreeRTOS heap）

来源：`STM32F407XX_FLASH.ld`

- `STM32F407XX_FLASH.ld:66`：`_Min_Heap_Size = 0x200`（512 bytes）
- `STM32F407XX_FLASH.ld:67`：`_Min_Stack_Size = 0x400`（1024 bytes）

说明：这两个是链接脚本里为 “用户堆/主栈” 预留的保底空间；**FreeRTOS 的动态分配走 `configTOTAL_HEAP_SIZE`（heap_4）**，两者不是一套。

## 3) 工程内额外静态内存池（非 FreeRTOS heap）

来源：`components/memory_allocation/MemoryAllocation.h` + `components/memory_allocation/MemoryAllocation.c`

- `components/memory_allocation/MemoryAllocation.h:8`：`MEMORY_POND_MAX_SIZE = 8192`（注释写“kb”，但实际是 **bytes**）
- `components/memory_allocation/MemoryAllocation.c:6`：`static uint8_t MemoryPond[MEMORY_POND_MAX_SIZE];`

用途（当前工程里主要是 RingBuffer/Log/AT 用）：

- `components/ring_buffer/RingBuffer.c:21`：`CreateRingBuffer()` 使用 `static_alloc()` 从 `MemoryPond` 分配
- `Application/Src/Usart1_manage.c:22`：USART1 DMA ringbuffer 在 `MyUart_Init()` 中创建
- `components/log/log.c:153`：异步日志 ringbuffer 在 `Log_Init()` 中创建
- `components/AT/AT.c:34`：AT 设备的 ringbuffer 在 `AT_Init()` 中创建

## 4) 任务与栈大小清单（创建点 + 栈单位）

> 说明：  
> - `osThreadAttr_t.stack_size`（CMSIS-RTOS2）单位是 **bytes**。  
> - `xTaskCreate(..., usStackDepth, ...)` 的 `usStackDepth` 单位是 **word**（STM32F4 上 1 word = 4 bytes）。

### A. Core/Src/freertos.c 创建的 CMSIS-RTOS2 线程

| 任务名 | 创建点 | 栈大小 | 优先级 | 备注 |
|---|---|---:|---:|---|
| `KeyScanTask` | `Core/Src/freertos.c:280` | `sizeof(KeyScanTaskBuffer)=256*4=1024` bytes（`Core/Src/freertos.c:80`） | `osPriorityNormal`（`Core/Src/freertos.c:88`） | 静态分配（`cb_mem/stack_mem`）；周期调用 `KEY_Tasks()`（`Core/Src/freertos.c:334`） |
| `uartTask` | `Core/Src/freertos.c:283` | `sizeof(uartTaskBuffer)=128*4=512` bytes（`Core/Src/freertos.c:92`） | `osPriorityLow`（`Core/Src/freertos.c:101`） | 静态分配；当前任务逻辑主要为占位（消费 RingBuffer 的代码已注释） |
| `lcdTask` | `Core/Src/freertos.c:286` | `sizeof(lcdTaskBuffer)=384*4=1536` bytes（`Core/Src/freertos.c:104`） | `osPriorityLow`（`Core/Src/freertos.c:113`） | 静态分配；注意与 LVGL 可能冲突（见 `docs/developer-guide/modules/core-startup-rtos.md`） |
| `LightSensor_Task` | `Core/Src/freertos.c:285` | `256*4 = 1024` bytes（`Core/Src/freertos.c:113`） | `osPriorityNormal`（`Core/Src/freertos.c:114`） | 入口在 `Application/Src/Light_Sensor_task.c:15` |

已定义但当前注释掉（不创建）的线程：

- `MqttTask`：`Core/Src/freertos.c:287`（栈 `1024*4` bytes，见 `Core/Src/freertos.c:106`）
- `Water_Sensor_Task`：`Core/Src/freertos.c:290`（栈 `256*4` bytes，见 `Core/Src/freertos.c:121`）
- `lvgl_task`（legacy，当前未创建）：仅定义了属性 `Core/Src/freertos.c:146`（栈 `4096*2` bytes，见 `Core/Src/freertos.c:148`）
- `as608_test`/`rc522_test`/`rc522_my_test`（legacy，当前未创建）：仅定义了属性 `Core/Src/freertos.c:125`/`Core/Src/freertos.c:131`/`Core/Src/freertos.c:137`；创建语句目前注释在 `Core/Src/freertos.c:299`

### B. OSAL 组件创建的线程（内部映射到 CMSIS-RTOS2）

| 任务名 | 创建点 | 栈大小 | 优先级 | 备注 |
|---|---|---:|---:|---|
| `AT_Core_Task` | `components/AT/AT_Core_Task.c:92` | `256*10 = 2560` bytes（`components/AT/AT_Core_Task.c:87`） | `OSAL_PRIO_NORMAL`（`components/AT/AT_Core_Task.c:88`） | 由 `Core/Src/freertos.c:297` 调用 `at_core_task_init()` 触发 |
| `MQTT_AT` | `components/AT/AT_Core_Task.c:110` | `1024*4 = 4096` bytes（`components/AT/AT_Core_Task.c:107`） | `OSAL_PRIO_NORMAL`（`components/AT/AT_Core_Task.c:108`） | 由 `at_core_task_init()` 内部创建 |
| `LogTask` | `components/log/log.c:165` | `128*4 = 512` bytes（`components/log/log.c:160`） | `OSAL_PRIO_LOW`（`components/log/log.c:161`） | 仅 `LOG_ASYNC_ENABLE=1` 时创建 |

### C. Application 里直接创建的 FreeRTOS 任务（xTaskCreate）

| 任务名 | 创建点 | 栈深度 | 优先级 | 备注 |
|---|---|---:|---:|---|
| `lvgl_handler` | `Application/Src/lvgl_task.c:74` | `1024` word（≈4096 bytes） | `configMAX_PRIORITIES-3`（`Application/Src/lvgl_task.c:12`） | 唯一允许直接调用 LVGL API 的线程 |
| `lvgl_tick` | `Application/Src/lvgl_task.c:82` | `configMINIMAL_STACK_SIZE=128` word（≈512 bytes） | `configMAX_PRIORITIES-2`（`Application/Src/lvgl_task.c:16`） | 周期 `lv_tick_inc()` |
| `dev_init` | `Application/Src/lock_devices.c:74` | `768` word（≈3072 bytes） | `tskIDLE_PRIORITY+3` | 初始化 AS608/RC522 并置 ready bits |
| `lock_act` | `Application/Src/lock_actuator.c:123` 或 `Application/Src/lock_actuator.c:141` | `512` word（≈2048 bytes） | `tskIDLE_PRIORITY+2` | 执行器任务/队列，避免 UI 线程直驱硬件 |
| `fp_worker` | `Application/Src/ui_lock.c:2536` | `1024` word（≈4096 bytes） | `tskIDLE_PRIORITY+2` | 指纹业务 worker（依赖 `dev_init` 置 ready） |
| `rfid_worker` | `Application/Src/ui_lock.c:2541` | `1024` word（≈4096 bytes） | `tskIDLE_PRIORITY+2` | RFID 业务 worker（依赖 `dev_init` 置 ready） |

> 备注：`Application/Src/ui_fingerprint.c:261` 也会创建 `fp_worker`（`768` word），但该文件当前不作为主 UI 入口（主 UI 在 `ui_lock_init()`）。

### D. AS608 Service 内部线程（CMSIS-RTOS2）

| 任务名 | 创建点 | 栈大小 | 优先级 | 队列 |
|---|---|---:|---:|---:|
| `as608_svc` | `Drivers/BSP/as608/Core/Src/as608_service.c:239` | `AS608_SVC_TASK_STACK=1024` bytes（`Drivers/BSP/as608/Core/Src/as608_service.c:17`） | `osPriorityNormal`（`Drivers/BSP/as608/Core/Src/as608_service.c:21`） | `AS608_SVC_QUEUE_DEPTH=4`（`Drivers/BSP/as608/Core/Src/as608_service.c:13`） |

### E. FreeRTOS 内核自带任务（不是工程显式创建）

| 任务名 | 栈大小 | 备注 |
|---|---:|---|
| Idle Task | `configMINIMAL_STACK_SIZE=128` word | FreeRTOS 内核创建 |
| Timer Service Task | `configTIMER_TASK_STACK_DEPTH=256` word | `configUSE_TIMERS=1` 时创建 |

## 5) 非 CubeMX 初始化调用点（你后续要“保留但不调用”的位置）

### A. main() 中的用户初始化（调度器启动前）

来源：`Core/Src/main.c`

- `Core/Src/main.c:190`：`lcd_init();`
- `Core/Src/main.c:198`：`MyUart_Init();`
- `Core/Src/main.c:199`：`KEY_Init(&key0, &key0_config);`
- `Core/Src/main.c:200`：`KEY_Init(&key1, &key1_config);`
- `Core/Src/main.c:201`：`KEY_Init(&key2, &key2_config);`
- `Core/Src/main.c:203`：`HAL_Delay(100);`（属于 HAL，但这是“用户自己加的启动延时”）

### B. MX_FREERTOS_Init() 中的用户初始化（调度器启动前、线程创建后）

来源：`Core/Src/freertos.c`

- `Core/Src/freertos.c:292`：`Log_PortInit();`
- `Core/Src/freertos.c:293`：`Log_Init();`
- `Core/Src/freertos.c:294`：`LockDevices_Start();`（AS608/RC522）
- `Core/Src/freertos.c:295`：`LockActuator_Start();`（TIM3 CH1 PC6，MG90S）
- `Core/Src/freertos.c:297`：`at_core_task_init(&g_at_manager, &huart3);`（AT + MQTT_AT）
- `Core/Src/freertos.c:302`：`lvgl_init();`（创建 LVGL 两个 FreeRTOS task + UI 初始化）

### C. 任务内部的“初始化动作”（不是 CubeMX）

这些不是 “main/freertos 的显式 init 调用”，但属于启动后一定会发生的初始化逻辑：

- `Application/Src/lvgl_task.c:47`：`lv_init()` + `lv_port_disp_init()` + `lv_port_indev_init()` + `ui_lock_init()`
- `Application/Src/lock_devices.c:36`：`AS608_Port_BindUart(&huart4)` + `AS608_Service_Init()` + `RC522_Init()`
- `Application/Src/lock_actuator.c:46`：`mg90s_init()` + 初始 `mg90s_lock()`
- `Application/Src/Light_Sensor_task.c:16`：`LightSensor_Init()`（且在循环里又调用一次：`Application/Src/Light_Sensor_task.c:19`）
