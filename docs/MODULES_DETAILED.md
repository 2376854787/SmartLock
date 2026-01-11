# 模块详解（逐模块现状/差距/下一步）

> 本文档以“你的架构清单”为目标态，对照本仓库 As-Is 做差距分析，给出下一步落地点。

## 1) `core_base`（L3）
**职责**：错误码、编译器封装、通用宏、断言等“地基”能力。
- 位置：`components/core_base/`
- 关键文件：
  - `components/core_base/ret_code.h:1`
  - `components/core_base/compiler_cus.h:1`
  - `components/core_base/utils_def.h:1`
  - `components/core_base/assert_cus.h:1`、`components/core_base/assert_cus.c:1`
- 已具备：
  - 统一错误码类型 `ret_code_t`
  - `CORE_ASSERT/CORE_FAULT_ASSERT` 与可选 noinit 记录
  - 基础宏（MIN/MAX/CLAMP/ARRAY_SIZE/位操作/container_of）
- 主要差距：
  - `ret_code.h` 依赖 `cmsis_gcc.h`，与“编译器零依赖/跨平台”冲突。
  - `compiler_cus.h` 与目标态 `compiler.h` 的宏命名不完全一致（需要对齐一套统一宏）。
- 下一步（2h 粒度的 DoD）：
  - 统一 `compiler` 宏命名与最小集合，并列出允许直接使用的宏清单。
  - 将 `ret_code_t` 从 CMSIS 依赖中剥离，并统一错误码命名规范。

## 2) `memory_allocation`（静态内存池，L2/L3）
**职责**：提供受控的静态分配，替代 malloc/free。
- 位置：`components/memory_allocation/`
- 关键文件：`components/memory_allocation/MemoryAllocation.c:1`、`components/memory_allocation/MemoryAllocation.h:1`
- 已具备：线性静态池 + 对齐分配。
- 主要差距：
  - 单例全局池：不符合“多实例/可裁剪/可复用库”目标。
  - 与 `APP_config.h` 的依赖会扩大耦合面。
- 下一步（DoD）：
  - 文档化“内存策略”：哪些模块允许使用该池、生命周期、初始化时机、溢出处理。
  - 规划为句柄化内存池接口（buffer 注入）。

## 3) `ring_buffer`（L3 核心）
**职责**：高性能、DMA 友好、中断安全的环形缓冲区；支持零拷贝接口。
- 位置：`components/ring_buffer/`
- 关键文件：`components/ring_buffer/RingBuffer.h:1`、`components/ring_buffer/RingBuffer.c:1`、`components/ring_buffer/rb_port.h:1`
- 已具备：
  - 2 的幂优化路径（`& (size - 1)`）
  - `WriteReserve/Commit` 与 `ReadReserve/Commit` 等零拷贝 API
  - 通过 `rb_port.h` 抽象临界区（走 OSAL 或 PRIMASK）
- 主要差距：
  - 创建时直接用 `static_alloc` 分配 buffer（与“零耦合/可裁剪”冲突）。
  - 目前未见 PC 单测框架集成（目标要求核心模块 100% 分支覆盖）。
- 下一步（DoD）：
  - 文档明确：满/空判定策略、ForceWrite/ForceRead 的语义与风险。
  - 规划“外部注入 buffer”的创建模式（或 allocator vtable）。

## 4) `osal`（L2）
**职责**：兼容裸机与 RTOS 的 OS 抽象：临界区、互斥、信号量、消息队列、线程与 flags。
- 位置：`components/osal/`
- 关键文件：`components/osal/osal.h:1`、`components/osal/osal_cmsis2.c:1`、`components/osal/osal_config.h:1`
- 已具备：
  - CMSIS-RTOS2 后端实现
  - `OSAL_is_timeout` 等超时工具函数
- 主要差距：
  - `osal_config.h` 为空：后端选择/裁剪策略未固化。
  - `platform/STM32/osal/osal_port.c:1` 为空：平台端口层尚未形成闭环。
- 下一步（DoD）：
  - 文档化 OSAL 的“后端矩阵”（Baremetal/FreeRTOS/CMSIS2/PC mock）与差异。
  - 明确 ISR 可调用 API 列表（哪些函数 ISR-safe）。

## 5) `hal`（L2：GPIO/UART/Time）
**职责**：硬件抽象，隔离业务与芯片/HAL 库。
- 位置：`components/hal/include/` + `platform/STM32/ports/`
- `hal_time`：
  - `components/hal/include/hal_time.h:1`、`platform/STM32/ports/hal_time_port.c:1`
  - 状态：已实现（DWT + 退化路径），基本可用。
- `hal_gpio` / `hal_uart`：
  - `components/hal/include/hal_gpio.h:1`、`components/hal/include/hal_uart.h:1` 目前为空
  - `platform/STM32/ports/hal_gpio_port.c:1`、`platform/STM32/ports/hal_uart_port.c:1` 目前为空
- 下一步（DoD）：
  - 文档先行：定义 API（非阻塞语义、回调模型、错误码、线程/ISR 约束），再落实现与测试。

## 6) `log`（L3）
**职责**：分级日志、异步输出、Hexdump；生产可降级。
- 位置：`components/log/`
- 关键文件：`components/log/log.h:1`、`components/log/log.c:1`、`components/log/log_port.c:1`、`components/log/ReadME.md:1`
- 已具备：异步 RingBuffer + 后台任务 flush + Hexdump。
- 主要差距（对“跨平台/零耦合”）：
  - 直接调用 `HAL_GetTick()`；port 直接绑定 `UART_HandleTypeDef` 与 STM32 DMA。
- 下一步（DoD）：
  - 文档定义“log 的可移植边界”：时间源与输出后端必须可注入（现已具备 backend 注入雏形）。

## 7) `container/list`（L2）
**职责**：侵入式双向循环链表（参考 Linux list_head）。
- 位置：`components/container/include/list_cus.h:1`
- 状态：占位文件（空）。
- 下一步（DoD）：
  - 输出 API 草案（init/add/del/for_each_entry），并给 SoftTimer/EventBus 复用。

## 8) `soft_timer`（L2）
**职责**：软件定时器（单次/周期），建议基于链表管理。
- 位置：`components/soft_timer/include/soft_timer.h:1`、`components/soft_timer/src/soft_timer.c:1`
- 状态：占位文件（空）。
- 下一步（DoD）：
  - 文档先确定“tick 源”与“调度模型”（回调上下文：任务态/ISR 禁止等）。

## 9) `auto_init`（L3）
**职责**：用 linker section 自动调用 init 函数，消除 main 里长串 init。
- 位置：`components/auto_init/include/auto_init.h:1`、`components/auto_init/src/auto_init.c:1`
- 状态：占位文件（空）。
- 下一步（DoD）：
  - 文档先确定：section 名称、导出宏、启动遍历时机（在 OS 启动前/后）。

## 10) `AT`（L2：协议栈）
**职责**：AT 命令管理与解析（含 URC、DMA idle 接收等）。
- 位置：`components/AT/`
- 关键文件：`components/AT/AT.h:1`、`components/AT/AT.c:1`
- 已具备：功能较完整，但耦合较重。
- 主要差距：
  - 强耦合 STM32 HAL；不符合“句柄隐藏/零耦合/可移植”。
  - 仓库已有重构待办清单：`components/AT/??? AT 框架与应用重构待办事项清单 (Master To-Do List).md:1`
- 下一步（DoD）：
  - 先做“适配器接口（AT_Adaptor_Ops）”与所有权/竞态修复方案设计，再落地实现。

