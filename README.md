# SmartClock / SmartLock（STM32F407 智能门锁面板）

这是一套运行在 **STM32F407** 上的带屏幕“门锁控制面板”固件：它把 **本地开锁（指纹 / RFID / 密码）**、**触摸 UI**、以及 **联网（Wi‑Fi + MQTT，持续完善中）** 集成到一块板子上，让“开门/管理权限/查看状态”更直观。

## 文档导航（按功能模块）

建议按“功能”而不是“文件夹”阅读；每个模块都给出**代码入口**。

- **系统启动与任务调度（RTOS）**
  - 入口：`Core/Src/main.c`
  - 任务创建：`Core/Src/freertos.c`
  - RTOS 配置：`Core/Inc/FreeRTOSConfig.h`
- **图形界面（LVGL）与显示/触摸**
  - LVGL 任务与初始化：`Application/Src/lvgl_task.c`
  - 主 UI（门锁页面/流程）：`Application/Src/ui_lock.c`
  - 指纹 UI：`Application/Src/ui_fingerprint.c`
  - LVGL 显示/触摸移植层：`Drivers/BSP/lvgl_port/lvgl_port.c`
  - 触摸驱动（GT9xxx/GT9147）：`Drivers/BSP/touch/gt9xxx.c`
  - LVGL 配置：`Middlewares/LVGL/lv_conf.h`
  - UI 快速笔记：`Application/Src/readme.txt`
- **门锁业务数据（本地凭据管理）**
  - 数据结构/接口：`Application/Inc/lock_data.h`
  - 实现：`Application/Src/lock_data.c`
- **指纹模块（AS608）**
  - BSP/第三方驱动：`Drivers/BSP/as608/README.md`
  - 任务（测试/联调）：`Application/Src/as608_test_task.c`
- **RFID 模块（RC522）**
  - 驱动：`Drivers/BSP/rc522/rc522_my.c`
  - 任务（测试/联调）：`Application/Src/rc522_my_test_task.c`
- **联网与云端（ESP-01S + AT + MQTT）**
  - Wi‑Fi 模块驱动：`Drivers/BSP/ESP01s/ESP01S.c`
  - AT 框架：`components/AT/AT.c`
  - 相关任务入口：`Application/Inc/wifi_mqtt_task.h`、`Application/Src/wifi_mqtt_task.c`
- **人机输入与传感器**
  - 按键：`Drivers/BSP/Keys/KEY.c`
  - 光照：`Drivers/BSP/Light_Sensor/LightSeneor.c`、`Application/Src/Light_Sensor_task.c`
  - 水滴/液体检测（ADC）：`Application/Src/water_adc.c`
- **通用基础组件（可复用）**
  - 日志：`components/log/log.c`、`components/log/ReadME.md`
  - 环形缓冲区：`components/ring_buffer/RingBuffer.c`
  - OS 抽象层（CMSIS-RTOS2）：`components/osal/osal_cmsis2.c`
  - 软定时器：`components/soft_timer/src/soft_timer.c`
  - 其他：`components/hfsm/`、`components/auto_init/`、`components/core_base/`
- **工程与构建**
  - CMake 工程：`CMakeLists.txt`、`CMakePresets.json`
  - CubeMX 工程：`SmartLock.ioc`
  - 启动文件/链接脚本：`startup_stm32f407xx.s`、`STM32F407XX_FLASH.ld`

## 技术概览

- **MCU**：STM32F407ZGT6（Cortex‑M4F）
- **RTOS**：FreeRTOS Kernel **V10.3.1**（通过 **CMSIS‑RTOS v2**：`cmsis_os2`）
- **GUI**：LVGL **v8.3.6**
- **HAL/CMSIS**：STM32CubeF4 HAL（`Drivers/STM32F4xx_HAL_Driver`）+ CMSIS（`Drivers/CMSIS`）
- **构建方式**：CMake（适配 GCC 工具链），同时保留 CubeMX `.ioc` 配置源

## 阅读路径（不同读者）

- **如果你是“使用者/演示者”（关心能做什么）**
  - 先看：`Application/Src/readme.txt`（UI/触摸/LVGL 启动路径速览）
  - 再看：`Application/Src/ui_lock.c`（页面结构与交互流程）
  - 需要“管理权限/凭据”时看：`Application/Src/lock_data.c`
- **如果你是“开发者/维护者”（关心怎么改、怎么扩）**
  - 从系统入口开始：`Core/Src/main.c` → `Core/Src/freertos.c`
  - UI 链路：`Application/Src/lvgl_task.c` → `Drivers/BSP/lvgl_port/lvgl_port.c` → `Application/Src/ui_lock.c`
  - 外设联调入口：
    - 指纹：`Application/Src/as608_test_task.c`、`Drivers/BSP/as608/`
    - RFID：`Application/Src/rc522_my_test_task.c`、`Drivers/BSP/rc522/`
    - Wi‑Fi/MQTT：`Drivers/BSP/ESP01s/`、`components/AT/`

## 重要提示（避免踩坑）

- **触摸坐标镜像/点不准**：优先检查 `Drivers/BSP/touch/gt9xxx.c` 的映射逻辑（项目笔记见 `Application/Src/readme.txt`）。
- **启用 LVGL 组件后编译报缺符号**：本项目在 `CMakeLists.txt` 中**手工列举** LVGL 源文件；修改 `Middlewares/LVGL/lv_conf.h` 后，可能还需要同步更新 CMake 的源文件列表。

