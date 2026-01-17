# SmartClock / SmartLock 工程说明（文档入口）

本仓库当前形态是 **STM32F4 + FreeRTOS 的产品工程**（CubeMX 生成目录为 `Core/`、`Drivers/`、`Middlewares/`），并在 `components/` 下逐步沉淀可复用的通用 C 组件（日志、OSAL、RingBuffer 等）。

你给出的目标（v1.0 Commercial Ready）是构建一套 **MISRA C 风格、零耦合、可裁剪、跨平台（MCU/PC）** 的通用 C 工具库。本仓库的“通用库化”仍在演进中：Phase 1 基础设施基本具备，Phase 2 关键模块（HAL/list/soft_timer/auto_init）多数仍为占位文件。

## 文档导航
- `docs/PROJECT_OVERVIEW.md`
- `docs/ARCHITECTURE_GUIDE.md`
- `docs/MODULE_INDEX.md`
- `docs/STATUS_AS_IS.md`
- `docs/CHECKLIST.md`
- `docs/DAILY_PLAN_2H.md`

