# 每日 2 小时执行计划（从 2025-12-06 起）

> 使用方式（建议固定节奏）：
> - 10m：回顾昨天 + 明确今天 DoD（完成标准）
> - 80m：实现（代码/接口/重构）
> - 20m：验证（最小编译/最小自测/最小跑通）
> - 10m：记录（更新 checklist + 写 3 行变更说明）
>
> 顺序原则：**先 Phase 1/2 打磨到“可移植且可测”**，再推进 EventBus/CRC/CLI/Config 等 Phase 3/4。

## Week 1（12-06 ~ 12-12）：Phase 1 体检 + “跨平台/零耦合”红线确立
- 2025-12-06：盘点现状与依赖边界（输出：`docs/STATUS_AS_IS.md` 更新为“可行动”版本：列 Top10 耦合点与风险）
- 2025-12-07：统一错误码规范（DoD：确定全局错误码命名与语义；列出需要全仓替换的差异点）
- 2025-12-08：`ret_code_t` 去平台依赖设计（DoD：`ret_code.h` 不再 include CMSIS/STM32；记录迁移步骤）
- 2025-12-09：`compiler` 宏对齐设计（DoD：明确 `__PACKED/__WEAK/__ALIGN/__SECTION/BARRIER` 的统一命名与使用禁令）
- 2025-12-10：`utils_def` 的 MISRA 风险清单（DoD：宏副作用/多次求值风险条目写入文档并给出替代建议）
- 2025-12-11：RingBuffer 解耦策略确定（DoD：决定“外部注入 buffer”或“allocator vtable”，写入 `docs/MODULES_DETAILED.md`）
- 2025-12-12：建立“PC 可测”的构建拆分方案（DoD：在文档中确定 CMake 目标拆分：components/app/tests）

## Week 2（12-13 ~ 12-19）：Phase 2（HAL）先接口后实现
- 2025-12-13：设计 `hal_gpio` API（DoD：write/read/toggle + 句柄/port 定义 + 线程/ISR 约束）
- 2025-12-14：实现 STM32 `hal_gpio_port`（DoD：最小读写可编译；不暴露 HAL 结构体到上层）
- 2025-12-15：设计 `hal_uart` 非阻塞 API（DoD：send_async/recv_async/set_callback，定义 busy/timeout 语义）
- 2025-12-16：实现 STM32 `hal_uart_port`（DoD：DMA/IT 发送可回调；Rx idle 事件可上报）
- 2025-12-17：把 `log_port` 改走 `hal_uart`（DoD：`log_port.c` 不再 include `usart.h`/`stm32f4xx_hal.h`）
- 2025-12-18：把 `log` 的 tick 走 `hal_time`（DoD：`log.c` 不再直接 `HAL_GetTick`）
- 2025-12-19：HAL 模块自检（DoD：HAL 头文件无 STM32 include；platform 端口集中在 `platform/STM32/ports`）

## Week 3（12-20 ~ 12-26）：Phase 2（容器 + 调度 + auto-init）
- 2025-12-20：实现侵入式链表 `list_head`（DoD：init/add/del/for_each_entry 可用）
- 2025-12-21：为 SoftTimer 选择数据结构与回调上下文（DoD：明确“回调只在任务态执行/ISR 禁止”或给出 ISR-safe 方案）
- 2025-12-22：实现 `soft_timer` 基础（DoD：one-shot + cancel 可用，tick 驱动接口明确）
- 2025-12-23：实现 `soft_timer` 周期定时（DoD：run_every 可用；超时判断使用 `hal_time`/`OSAL_is_timeout`）
- 2025-12-24：设计 `auto_init` section 宏（DoD：定义 `INIT_COMPONENT_EXPORT(fn)` 与遍历入口）
- 2025-12-25：实现 `auto_init`（DoD：STM32 链接脚本可用；给出 PC 端 stub 策略）
- 2025-12-26：Phase 2 总结复盘（DoD：更新 `docs/CHECKLIST.md`，Phase 2 进入条件明确）

## Week 4（12-27 ~ 2026-01-02）：Build & Test（让“可裁剪/可移植”可验证）
- 2025-12-27：CMake 拆分：components/app（DoD：components 能独立编译为静态库）
- 2025-12-28：加入 host 构建（DoD：PC 端能编译 components，不依赖 STM32 头）
- 2025-12-29：引入 Unity（DoD：tests 目标能跑一个空测试）
- 2025-12-30：RingBuffer 单测（DoD：基础读写/环绕/满空边界通过）
- 2025-12-31：MemoryAllocation 单测（DoD：对齐/溢出/复位通过）
- 2026-01-01：OSAL mock 方案确定（DoD：明确 PC 端 OSAL 替代策略/最小实现点）
- 2026-01-02：Week4 复盘（DoD：核心模块测试入口写入文档；更新 Phase 3 前置条件）

## Week 5（01-03 ~ 01-09）：Phase 3（EventBus/CRC/CLI）先做“库化核心”
- 2026-01-03：EventBus API 设计（DoD：sync/async 模式与线程边界写清）
- 2026-01-04：EventBus sync 实现 + 测试（DoD：订阅/发布/取消订阅可测）
- 2026-01-05：EventBus async 实现（DoD：队列后端走 OSAL msgq；溢出策略明确）
- 2026-01-06：CRC16 实现 + 测试向量（DoD：标准向量通过）
- 2026-01-07：CRC32 实现 + 测试向量（DoD：标准向量通过）
- 2026-01-08：CLI API 设计（DoD：命令注册/argc argv 解析/输出后端抽象）
- 2026-01-09：CLI 最小实现（DoD：至少 2 个内置命令可跑在 PC）

## Week 6（01-10 ~ 01-16）：Phase 4（Config/TLV/存储抽象）
- 2026-01-10：Flash 抽象接口设计（DoD：read/write/erase 对齐与返回码约束）
- 2026-01-11：Config 格式设计（DoD：magic/version/crc + 迁移策略）
- 2026-01-12：Config pack/unpack（DoD：PC 端 RAM mock 可测）
- 2026-01-13：TLV 协议格式与 escaping 规则确定（DoD：帧边界/转义/CRC 流程写清）
- 2026-01-14：实现 TLV 编解码（DoD：单测覆盖“转义/粘包/拆包”）
- 2026-01-15：实现 CRC 校验集成（DoD：header+payload+crc 流程可测）
- 2026-01-16：Phase 4 复盘（DoD：Config/TLV 的接口稳定并写入模块文档）

## Week 7（01-17 ~ 01-23）：AT 模块“止血 + 解耦”
- 2026-01-17：AT 适配器接口落地（DoD：AT 核心不 include STM32 HAL）
- 2026-01-18：句柄隐藏（DoD：L2 以上不暴露结构体成员）
- 2026-01-19：修 UAF/超时竞态（DoD：超时不直接 free；由 core task 回收）
- 2026-01-20：ISR 下放（DoD：ISR 只搬运 DMA 块，不做逐字节解析）
- 2026-01-21：URC 路由表（DoD：按前缀分发回调）
- 2026-01-22：AT PC mock 测试（DoD：至少覆盖 OK/ERROR/URC/超时）
- 2026-01-23：AT 复盘（DoD：把重构后依赖图写入 `docs/MODULES_DETAILED.md`）

## Week 8（01-24 ~ 01-30）：Commercial Ready（质量与安全基础）
- 2026-01-24：静态分析工具链选型与基线（DoD：cppcheck/clang-tidy/MISRA 工具的流程文档）
- 2026-01-25：清理高风险告警（DoD：0 个高危 UB/越界/未初始化告警）
- 2026-01-26：HardFault 捕获方案落地（DoD：寄存器现场记录 + 日志输出策略）
- 2026-01-27：黑匣子日志策略（DoD：生产模式最低成本记录 error）
- 2026-01-28：覆盖率目标（DoD：RingBuffer/CRC/Protocol 核心分支覆盖接近 100%）
- 2026-01-29：发布验收清单（DoD：`docs/CHECKLIST.md` 可作为 Release Gate）
- 2026-01-30：v1.0 复盘与冻结接口（DoD：接口冻结、偏离清单归档）
