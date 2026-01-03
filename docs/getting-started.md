# 快速开始（构建 / 烧录 / 联调 / 排错）

本文面向第一次上手本工程的同学，目标是：

1) 你能把固件编译出来并烧到板子上跑起来  
2) 遇到“卡死/没日志/触摸失灵/云端不通/堆不够”等问题时，能最快定位

## 1. 工程结构与入口

- 工程配置：`SmartLock.ioc`（CubeMX 引脚/外设/FreeRTOS 任务配置的权威来源）
- 启动入口：`Core/Src/main.c`
- RTOS 初始化入口：`Core/Src/freertos.c`
- LVGL 入口：`Application/Src/lvgl_task.c`
- 云端任务：`Application/Src/mqtt_at_task.c`
- 门锁 UI：`Application/Src/ui_lock.c`
- 执行器（舵机）队列任务：`Application/Src/lock_actuator.c`
- 指纹/RFID 初始化任务：`Application/Src/lock_devices.c`

## 2. 构建（CMake Presets）

工程提供 `CMakePresets.json`（Ninja + `cmake/gcc-arm-none-eabi.cmake`）。

### 2.1 Debug

```bash
cmake --preset Debug
cmake --build --preset Debug
```

### 2.2 Release

```bash
cmake --preset Release
cmake --build --preset Release
```

产物（以 Debug 为例）：

- `build/Debug/SmartLock.elf`
- `build/Debug/SmartLock.hex`
- `build/Debug/SmartLock.bin`

## 3. 烧录与运行

### 3.1 STM32CubeProgrammer（GUI）

1) 连接 ST-LINK（SWD）  
2) 选择 `build/<preset>/SmartLock.hex`  
3) Download + Reset

### 3.2 STM32CubeProgrammer（CLI 示例）

```bash
STM32_Programmer_CLI -c port=SWD -w build/Debug/SmartLock.hex -v -rst
```

## 4. 必查配置（上电前）

### 4.1 华为云 IoTDA（如需云端联调）

编辑 `Application/Inc/huawei_iot_config.h`：

- `HUAWEI_IOT_DEVICE_ID`
- `HUAWEI_IOT_DEVICE_SECRET`
- `HUAWEI_IOT_MQTT_HOST` / `HUAWEI_IOT_MQTT_PORT`
- `HUAWEI_IOT_NTP_SERVER` / `HUAWEI_IOT_TIMEZONE`

### 4.2 触摸坐标映射（点不准/镜像）

优先检查 `components/core_base/config_cus.h`：

- `TOUCH_*_INVERT_X`
- `TOUCH_*_INVERT_Y`

并结合触摸驱动 `Drivers/BSP/touch/gt9xxx.c` 的映射逻辑排查。

## 5. 常见问题：最快定位路径

### 5.1 “卡死但没栈溢出日志”

常见原因是 **堆不够 / malloc 失败 / 某处阻塞等待**，但日志未必能打出来（尤其是异步日志或中断上下文）。

建议直接用断点定位：

1) `Core/Src/freertos.c` 的 `vApplicationMallocFailedHook()`  
2) `Core/Src/freertos.c` 的 `vApplicationStackOverflowHook()`  
3) `configASSERT()`（见 `Core/Inc/FreeRTOSConfig.h`）

同时在 `CubeIDE/Keil` 里打开 FreeRTOS 运行时信息（Tasks/Heap），或在任务里临时打印：

- `uxTaskGetStackHighWaterMark(NULL)`（栈余量）
- `xPortGetFreeHeapSize()`（剩余堆）
- `xPortGetMinimumEverFreeHeapSize()`（历史最小剩余堆）

### 5.2 “日志提示 RTOS 调度器没启动”

这是本工程的正常现象：很多初始化在 `osKernelStart()` 之前执行，此时 OSAL/FreeRTOS 判断内核未运行会打印提示。

定位建议：

- 把耗时外设初始化放到 RTOS 任务中执行（工程已采用 `dev_init` / `lock_act`）
- 仅保留 CubeMX 生成的 `MX_*_Init()` 在 `main()` 里调用

### 5.3 “触摸初始化打印到 CTP ID 后卡死”

触摸驱动使用软 I2C + `delay_us()`（见 `Drivers/BSP/touch/ctiic.c`），如果微秒延时的实现依赖未就绪的时基，可能出现“无日志卡死”。

建议排查：

- `Drivers/System/Src/My_delay.c` 的 `delay_us()` 实现与时基依赖
- 触摸 I2C 引脚是否被复用/焊接问题（见 `docs/hardware.md`）
- I2C 上拉、电源时序

### 5.4 “云端命令下发了但门没动”

先区分两个概念：

- MQTT 命令收到了（看是否有 ack / response）
- 执行器队列是否就绪（`LockActuator_Start()` 是否运行、队列是否满）

对照：

- 协议：`docs/mqtt-control.md`
- 执行器任务：`Application/Src/lock_actuator.c`
- 命令处理：`Application/Src/mqtt_at_task.c`

### 5.5 “舵机抖动/电源重启”

MG90S 需要瞬时电流较大，常见问题是：

- 5V 供电不足或压降导致 MCU 复位
- 舵机与 MCU 未共地
- PWM 线过长/干扰

建议：舵机单独 5V 供电 + 共地，必要时加电容与滤波。

