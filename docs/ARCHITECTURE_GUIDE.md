# 架构与分层指南（对齐“架构清单”）

## 1) 分层定义（建议）
为了达成“零耦合、可裁剪、跨平台”，建议将工程拆成以下层次：

### L3：Core Infrastructure（地基层）
**只做通用能力**，禁止依赖具体 MCU HAL/RTOS。
- `core_base`：`ret_code_t`、`compiler` 封装、`utils_def`、`assert`
- `datastruct`：RingBuffer（建议允许外部注入 buffer 或注入 allocator）

### L2：HAL & OSAL（隔离层）
**业务与硬件/OS 的隔离墙**。
- HAL：GPIO/UART/Time 等硬件抽象，接口放 `.h`，实现放 `platform/<chip>/ports/*.c`
- OSAL：线程/互斥/信号量/消息队列/临界区/时间转换等

### L2：Service & Architecture（解耦层）
- log：分级/异步/Hexdump/生产降级（黑匣子）
- soft_timer：软件定时器
- event_bus：发布订阅
- cli：命令行
- crc/config/fault：可靠性与诊断

## 2) 依赖规则（必须遵守）
建议以“只允许向下依赖”为硬规则：
- `core_base` **不得** include `stm32*` / `cmsis_os*` / `FreeRTOS*`
- `datastruct` **不得**依赖具体 HAL；如需临界区仅通过 `rb_port.h`/OSAL 注入
- `service`（log、soft_timer、event_bus）只依赖 `core_base + osal + hal_time/hal_uart` 等抽象接口
- `Application` 只依赖抽象层接口（HAL/OSAL + service），尽量不直接依赖平台端口实现

## 3) OOC 句柄与“结构体隐藏”
目标态（建议）：
- `.h`：只暴露 `typedef struct xxx_s *xxx_handle_t;` 与 API
- `.c`：定义 `struct xxx_s { ... }`，对外不可见

## 4) 统一错误码体系
当前仓库已有 `ret_code_t`（见 `components/core_base/ret_code.h:1`）。
目标态建议：
- 统一语义与命名（`RET_ERR_PARAM/RET_ERR_TIMEOUT/...` 或维持现有 `RET_E_*`，但需全局一致）
- 规则：**功能函数统一返回 `ret_code_t`**；输出数据通过指针参数传出

## 5) MISRA 关键约束（清单化）
建议将以下作为“工程宪法”强制检查项：
- 禁止动态内存（除初始化阶段受控内存池）
- 禁止递归
- `switch` 必须 `default`
- 不使用不确定位宽类型（循环变量可例外）；统一 `<stdint.h>/<stdbool.h>`
- 指针地址运算用 `uintptr_t`
- 不直接出现 `__attribute__/#pragma pack` 等编译器关键字：统一走 `compiler` 封装

