# AS608 HAL + CMSIS-RTOS2 CRUD 模板

该模板基于 LibDriver 的 `driver_as608.c/h`，并增加：
- `as608_port.c/h`：HAL UART + RX 环形缓冲区 + LibDriver interface 适配。
- `as608_service.c/h`：CMSIS-RTOS2 任务化封装，提供基本 CRUD（录入/搜索/覆盖/删除）。

## 目录结构

```
ThirdParty/LibDriver_AS608/
  driver_as608.c
  driver_as608.h
  driver_as608_interface.h
Core/Inc/
  as608_port.h
  as608_service.h
Core/Src/
  as608_port.c
  as608_service.c
```

## 1. CubeMX / 工程配置（关键点）

1) 选择一个 UART 专用于 AS608（推荐 USART2/USART3）。
- BaudRate: **57600**（若你的模块被改过波特率，请改为实际值）
- 8N1, 无流控
- 使能 NVIC: USARTx global interrupt

2) 如果使用 FreeRTOS（CMSIS-RTOS2）：
- 确保 `CMSIS_V2` 启用
- Tick 建议 1ms（默认即可）

3) 将 `ThirdParty/LibDriver_AS608` 与 `Core/*` 加入编译。
- Include Path 添加：
  - `ThirdParty/LibDriver_AS608`
  - `Core/Inc`

## 2. 串口 RX 回调桥接（必须做）

在你工程中**唯一**的 `HAL_UART_RxCpltCallback` 中转发：

```c
#include "as608_port.h"

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    AS608_Port_OnUartRxCplt(huart);

    /* 你其他 UART 的处理... */
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    AS608_Port_OnUartError(huart);
}
```

## 3. 启动服务

在 `osKernelStart()` 之后、任意任务中调用（例如默认任务里）：

```c
#include "as608_port.h"
#include "as608_service.h"

extern UART_HandleTypeDef huart2; // 你选择的 UART

void StartDefaultTask(void *argument)
{
    AS608_Port_BindUart(&huart2);

    // addr=0xFFFFFFFF, password=0
    if (AS608_Service_Init(0xFFFFFFFFu, 0x00000000u) != AS608_SVC_OK)
    {
        // 初始化失败：重点检查 UART RX 是否正常、波特率、供电、TX/RX 线序
    }

    for(;;)
    {
        osDelay(1000);
    }
}
```

## 4. CRUD 用法示例

### Create：录入 id=1

```c
as608_status_t st;
if (AS608_CRUD_Create(1, 20000, &st) == AS608_SVC_OK && st == AS608_STATUS_OK)
{
    // 录入成功
}
```

### Read：搜索当前手指

```c
uint16_t id = 0, score = 0;
as608_status_t st;
AS608_CRUD_Read(8000, &id, &score, &st);
// st == AS608_STATUS_OK 表示匹配到 id
// st == AS608_STATUS_NOT_FOUND 表示未匹配
```

### Update：覆盖录入 id=1

```c
as608_status_t st;
AS608_CRUD_Update(1, 20000, &st);
```

### Delete：删除 id=1

```c
as608_status_t st;
AS608_CRUD_Delete(1, &st);
```

### List：读取索引表

```c
uint8_t table[32];
as608_status_t st;
AS608_List_IndexTable(0, table, &st);
```

## 5. 初始化失败最常见原因

LibDriver 的 `as608_init()` 会发送 **READ_SYS_PARA(0x0F)** 并在约 300ms 后读取响应。如果你看到类似“RX frame failed / decode failed”，通常是：

- UART TX/RX 接反、GND 未共地
- 模块供电不稳定（需足够电流）
- 波特率不一致（模块可能被改过）
- RX 中断未开启 / 回调未桥接 / 环形缓冲区未接收到数据
- 串口电平不匹配（3.3V/5V）

