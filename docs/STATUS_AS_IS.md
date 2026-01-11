# 当前工程现状（基于仓库扫描的事实记录）

## 1) Phase 1（基础设施）现状
- 错误码：已有 `ret_code_t`（`components/core_base/ret_code.h:1`）。
- 通用宏：已有 `utils_def.h`（`components/core_base/utils_def.h:1`）。
- 编译器封装：已有 `compiler_cus.h`（`components/core_base/compiler_cus.h:1`）。
- 断言：已有 `assert_cus`（`components/core_base/assert_cus.h:1`、`components/core_base/assert_cus.c:1`）。
- 日志：已实现异步 RingBuffer + Hexdump（`components/log/log.c:1`、`components/log/log.h:1`、`components/log/ReadME.md:1`）。
- RingBuffer：实现完整度较高，含零拷贝接口（`components/ring_buffer/RingBuffer.c:1`、`components/ring_buffer/RingBuffer.h:1`）。
- OSAL：CMSIS-RTOS2 版本可用（`components/osal/osal_cmsis2.c:1`、`components/osal/osal.h:1`）。

## 2) Phase 2（HAL & 容器 & 调度）现状：主要阻塞点
以下模块已建目录，但多数为占位/空文件：
- HAL GPIO：`components/hal/include/hal_gpio.h:1`、`platform/STM32/ports/hal_gpio_port.c:1`
- HAL UART：`components/hal/include/hal_uart.h:1`、`platform/STM32/ports/hal_uart_port.c:1`
- 侵入式链表：`components/container/include/list_cus.h:1`
- SoftTimer：`components/soft_timer/include/soft_timer.h:1`、`components/soft_timer/src/soft_timer.c:1`
- Auto-Init：`components/auto_init/include/auto_init.h:1`、`components/auto_init/src/auto_init.c:1`

## 3) 关键“耦合点”（与目标态冲突）
为达成“跨平台/零耦合”，建议在后续按计划逐步依赖反转：
- `components/log/log.c:1` 直接调用 `HAL_GetTick()`（建议改为 `hal_time` 或可注入 time provider）
- `components/log/log_port.c:1` 直接依赖 STM32 `UART_HandleTypeDef` 与 `HAL_UART_Transmit_DMA`
- `components/AT/AT.h:1` 直接 include `stm32f4xx_hal.h` 并暴露 `UART_HandleTypeDef*`
- `components/ring_buffer/RingBuffer.c:1` 直接使用 `static_alloc`（建议改为外部注入 buffer 或 allocator 接口）
- `components/core_base/ret_code.h:1` include `cmsis_gcc.h`（建议移除以满足“编译器零依赖”）

