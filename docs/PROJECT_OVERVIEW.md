# 项目概述

## 1. 目标与版本
- Date：2025-12-06
- 版本：1.0（Commercial Ready，目标态）
- 目标：构建一套符合 MISRA C 风格、零耦合、可裁剪、跨平台（MCU/PC）、具备功能安全基础的通用 C 工具库。
- 适用范围：裸机/RTOS；从简单传感器读取到复杂 AT 协议栈/物联网项目。

## 2. 当前仓库定位（As-Is）
当前仓库仍以“产品工程”为主：
- `Application/`：应用任务与业务逻辑
- `Core/`：CubeMX 生成 + FreeRTOS glue（中断、外设初始化等）
- `Drivers/`、`Middlewares/`：HAL/CMSIS/FreeRTOS 等第三方
- `components/`：通用组件沉淀区（工具库雏形）
- `platform/`：平台端口层（STM32 端口目录已建立，但部分端口文件仍为空）

## 3. 构建现状
当前顶层 CMake 更偏向 MCU 工程：
- `CMakeLists.txt:1` 固定 `arm-none-eabi-gcc`，并将大量业务源文件直接加入一个可执行目标。
目标态（Commercial Ready）建议演进为：
- `components` 构建为可复用静态库（MCU 与 PC 共用）
- `app`（本工程业务）作为单独目标链接 `components`
- `tests`（PC 单测）单独目标，跑 Unity/Mock/覆盖率

## 4. 设计原则（摘要）
详细规范见 `docs/ARCHITECTURE_GUIDE.md`。核心摘要：
- 禁止动态内存（除初始化受控池）、禁止递归、`switch` 必有 `default`
- 统一错误码 `ret_code_t`，业务函数返回错误码，数据用指针参数输出
- 句柄驱动（L2 以上不暴露结构体成员），多实例支持，尽量无全局状态
- 编译器/平台差异通过 `compiler.h`/port 层封装，不在业务代码直接使用 `__attribute__/__pragma`

