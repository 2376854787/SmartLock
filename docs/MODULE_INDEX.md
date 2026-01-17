# 模块索引（As-Is + To-Be）

> 状态定义：`DONE`=可用且基本完整；`PARTIAL`=可用但有明显耦合/欠缺；`STUB`=占位/空实现；`TODO`=缺失

| 模块 | 层级 | 状态 | 主要职责 | 关键文件 | 主要依赖 |
|---|---:|---|---|---|---|
| `core_base` | L3 | PARTIAL | 编译器封装/错误码/通用宏/断言 | `components/core_base/*` | 目前含 CMSIS 依赖（需收敛） |
| `memory_allocation` | L2/L3 | PARTIAL | 静态线性分配池（受控内存） | `components/memory_allocation/*` | 依赖 `APP_config.h` |
| `ring_buffer` | L3 | PARTIAL | 环形缓冲区（含零拷贝 reserve/commit） | `components/ring_buffer/*` | 依赖 `static_alloc`（建议解耦） |
| `osal` | L2 | PARTIAL | OS 抽象（CMSIS-RTOS2 实现） | `components/osal/*` | `osal_config.h` 为空，port 未落地 |
| `log` | L3 | PARTIAL | 分级/异步/Hexdump 日志 | `components/log/*` | 直接用 `HAL_GetTick`/STM32 UART（建议改为 HAL 抽象） |
| `hal_time` | L2 | DONE | tick ms/us 抽象（STM32 DWT 优化） | `components/hal/include/hal_time.h`, `platform/STM32/ports/hal_time_port.c` | 依赖 OSAL/STM32 寄存器 |
| `hal_gpio` | L2 | STUB | GPIO 抽象 | `components/hal/include/hal_gpio.h`, `platform/STM32/ports/hal_gpio_port.c` | 当前为空 |
| `hal_uart` | L2 | STUB | UART 非阻塞抽象 + 回调 | `components/hal/include/hal_uart.h`, `platform/STM32/ports/hal_uart_port.c` | 当前为空 |
| `container/list` | L2 | STUB | 侵入式双向循环链表 | `components/container/include/list_cus.h` | 当前为空 |
| `soft_timer` | L2 | STUB | 软件定时器/调度 | `components/soft_timer/*` | 当前为空 |
| `auto_init` | L3 | STUB | linker section 自动初始化 | `components/auto_init/*` | 当前为空 |
| `AT` | L2 | PARTIAL | AT 协议栈/命令管理 | `components/AT/*` | 强耦合 STM32 HAL（待解耦） |
| `hfsm` | L2 | PARTIAL | 状态机支持 | `components/hfsm/*` | 日志依赖可裁剪 |

