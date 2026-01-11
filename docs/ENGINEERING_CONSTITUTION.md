# 工程宪法：商业级编码与架构规范（落库版）

本文件把“商业级规范”固化为本仓库的硬规则，用于 Code Review、自测与后续静态分析基线。

## 0.1 语言与合规性标准（MISRA 风格约束）
- 尽量遵循 MISRA C:2012（允许记录偏离，见 `docs/governance/DEVIATION_LOG.md`（后续计划创建））。
- 禁止动态内存分配：`malloc/free` 禁用（除初始化阶段受控内存池）。
- 禁止递归。
- `switch` 必须包含 `default` 分支。

## 0.2 类型系统
- 禁止使用不确定位宽类型：`int/short/long`（循环变量可例外）。
- 强制使用 `<stdint.h>`（`uint8_t/int32_t`）与 `<stdbool.h>`。
- 指针地址存储/运算使用 `uintptr_t`。

## 0.3 编译器零依赖（通过封装实现“一次写，到处编译”）
- 代码中禁止直接出现 `#pragma pack`、`__attribute__` 等编译器关键字。
- 必须通过统一封装头（目标态为 `compiler.h`，当前为 `components/core_base/compiler_cus.h:1`）提供：
  - `__PACKED` / `__WEAK` / `__ALIGN(n)` / `__SECTION(name)` / `BARRIER()`

## 0.4 面向对象 C（OOC）与句柄设计
- 句柄驱动：L2 以上模块禁止暴露结构体成员。
  - Header：`typedef struct uart_dev_s *uart_handle_t;`
  - Source：`struct uart_dev_s { ... };`
- 多实例支持：模块必须支持 `xxx_init(handle, config)`；严禁用全局变量存储状态（除非明确 Singleton 并写入文档）。

## 0.5 统一错误码体系
- 功能函数一律返回 `ret_code_t`，数据通过指针参数传出。
- 当前实现：`components/core_base/ret_code.h:1`（后续需收敛命名与跨平台依赖）。

## 0.6 文档与 DoD（交付定义）
每个模块至少提供：
- API 说明（含线程/ISR 约束、阻塞/非阻塞语义）
- 依赖边界（允许 include 哪些头）
- 可裁剪开关（若有）
- 最小示例（如何集成）
- 单测策略（PC 端优先）

