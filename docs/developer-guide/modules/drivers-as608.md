# 模块指南：AS608 指纹（端口层 + 服务封装）

本模块把 AS608（UART 指纹模块）封装为“可在 RTOS 多任务下稳定使用”的服务：

- `as608_port`：绑定 UART4，并把 HAL 回调集中转发到模块内部
- `as608_service`：在独立线程中串行访问 AS608，向上提供同步 CRUD API

相关路径：

- 端口层：`Drivers/BSP/as608/Core/Inc/as608_port.h`、`Drivers/BSP/as608/Core/Src/as608_port.c`
- 服务层：`Drivers/BSP/as608/Core/Inc/as608_service.h`、`Drivers/BSP/as608/Core/Src/as608_service.c`
- 第三方驱动：`Drivers/BSP/as608/ThirdParty/LibDriver_AS608/*`

## 1. 硬件与串口绑定

UART：UART4  
引脚：PC10(TX) / PC11(RX)（见 `SmartLock.ioc` 与 `Core/Inc/main.h`）

注意事项：

- 建议 AS608 独占 UART4（不要与其他模块复用）
- 串口波特率以 `SmartLock.ioc` 为准（当前 UART4 配置为 57600）

## 2. 初始化与 ready 机制（工程约定）

本工程不在 UI 里初始化 AS608，而在 `dev_init` 任务里统一初始化：

- 启动点：`Core/Src/freertos.c` 调用 `LockDevices_Start()`
- 初始化任务：`Application/Src/lock_devices.c:dev_init_task()`
  - `AS608_Port_BindUart(&huart4)`
  - `AS608_Service_Init(addr, password)`（常用 `0xFFFFFFFF` / `0x00000000`）
  - 初始化成功后设置 ready bit

使用方（UI/worker）必须先等待：

- `LockDevices_WaitAs608Ready(timeout_ms)`（例：5000ms）

目的：

- 避免“初始化位置移动就卡死”的时序问题
- 统一在 RTOS 启动后初始化，减少对 delay/SysTick 的隐式依赖

## 3. 线程模型（Service 串行化）

服务层内部使用 CMSIS-RTOS2：

- queue：`AS608_SVC_QUEUE_DEPTH`（默认 4）
- task：`AS608_SVC_TASK_STACK`（默认 1024 bytes）
- 串行访问：所有 CRUD 请求都排队由 service 线程执行

这样可以避免：

- 多任务并发读写导致帧交织
- “偶发死锁/偶发 decode failed”

## 4. HAL 回调转发要求（必须接入）

AS608 的接收是“1 字节循环接收”模型，因此必须在工程统一回调处转发：

- `HAL_UART_RxCpltCallback()` → `AS608_Port_OnUartRxCplt(huart)`
- `HAL_UART_ErrorCallback()` → `AS608_Port_OnUartError(huart)`

当前工程转发位置：

- `Core/Src/freertos.c:HAL_UART_RxCpltCallback`
- `Core/Src/freertos.c:HAL_UART_ErrorCallback`

## 5. 常用 API（上层调用）

建议调用位置：指纹 worker（例如 `Application/Src/ui_lock.c` 的 `fp_worker`）。

| API | 作用 | 备注 |
|---|---|---|
| `AS608_Service_Init(addr, password)` | 初始化服务 | 只在 `dev_init` 任务中调用 |
| `AS608_CRUD_Create(id, timeout_ms, out_status)` | 录入指纹 | 录入通常 15~30s |
| `AS608_CRUD_Read(timeout_ms, out_found_id, out_score)` | 搜索/验证 | 常用 8s |
| `AS608_CRUD_Delete(id)` | 删除指纹 | |
| `AS608_CRUD_ClearAll()` | 清库 | |
| `AS608_List_IndexTable(num, out_table[32])` | 读索引表 | 用于“枚举已录入” |
| `AS608_Get_Capacity()` | 容量 | UI 用于校验 id 范围 |

## 6. 常见坑（定位建议）

- 还没 ready 就调用 CRUD：先检查 `LockDevices_As608Ready()` 或等待 `LockDevices_WaitAs608Ready()`
- 串口收发被其他模块抢占：确认 UART4 只给 AS608 用
- 卡死但没日志：优先断点 `vApplicationMallocFailedHook()` 与 `configASSERT()`

*** Delete File: docs/developer-guide/modules/drivers-rc522.md
