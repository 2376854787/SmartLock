# 快速开始（构建 / 烧录 / 运行）

本文面向第一次上手本工程的同学：从“环境准备 → 编译 → 烧录 → 常见问题排查”给出最短路径。

## 1. 环境准备

### 1.1 必需工具

- CMake（建议 ≥ 3.22）
- Ninja（本工程 `CMakePresets.json` 默认使用 Ninja 生成器）
- Arm GNU Toolchain（`arm-none-eabi-gcc` / `arm-none-eabi-g++` / `arm-none-eabi-objcopy` 等需可在命令行直接运行）

### 1.2 烧录工具（二选一）

- STM32CubeProgrammer（推荐，GUI/CLI 都可）
- OpenOCD + ST-LINK（或同类 SWD 调试器）

### 1.3 可选工具

- STM32CubeMX：用于打开 `SmartLock.ioc` 查看外设/引脚配置，或重新生成 CubeMX 代码
- VS Code + CMake Tools：更方便的 CMake Preset 构建体验

## 2. 编译（CMake Presets）

工程已提供 `CMakePresets.json`（Ninja + `cmake/gcc-arm-none-eabi.cmake`）。

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

### 2.3 产物位置

以 Debug 为例，常见产物如下（以实际生成目录为准）：

- `build/Debug/SmartLock.elf`
- `build/Debug/SmartLock.hex`
- `build/Debug/SmartLock.bin`

## 3. 烧录与运行

### 3.1 使用 STM32CubeProgrammer（GUI）

1. 连接 ST-LINK（SWD）
2. 选择 `build/<preset>/SmartLock.hex`（例如 `build/Debug/SmartLock.hex`）
3. 下载（Download）并复位（Reset）运行

### 3.2 使用 STM32CubeProgrammer（CLI，示例）

```bash
STM32_Programmer_CLI -c port=SWD -w build/Debug/SmartLock.hex -v -rst
```

> 说明：不同安装路径/版本的 CLI 名称可能略有差异；以本机 CubeProgrammer 安装为准。

## 4. 关键配置（上电前建议检查）

### 4.1 云端（华为云 IoTDA）

编辑 `Application/Inc/huawei_iot_config.h`：

- `HUAWEI_IOT_DEVICE_ID`：设备 ID
- `HUAWEI_IOT_DEVICE_SECRET`：设备密钥
- `HUAWEI_IOT_MQTT_HOST` / `HUAWEI_IOT_MQTT_PORT`：区域接入点与端口
- `HUAWEI_IOT_NTP_SERVER` / `HUAWEI_IOT_TIMEZONE`：SNTP 校时服务器与时区

### 4.2 触摸坐标（点不准/镜像）

触摸映射开关在 `components/core_base/config_cus.h`：

- `TOUCH_*_INVERT_X`
- `TOUCH_*_INVERT_Y`

并配合触摸驱动 `Drivers/BSP/touch/gt9xxx.c` 的映射逻辑排查。

## 5. 常见问题（最短排查）

- **触摸坐标镜像/点不准**：优先检查 `components/core_base/config_cus.h` 的映射开关与 `Drivers/BSP/touch/gt9xxx.c` 的坐标映射实现
- **启用 LVGL 组件后链接报缺符号**：本工程在 `CMakeLists.txt` 中手工列举 LVGL 源文件；修改 `Middlewares/LVGL/lv_conf.h` 后，可能还需要同步更新 CMake 的源文件列表
- **IoTDA 连不上**：
  - 先确认 SNTP 是否成功（`AT+CIPSNTPTIME?` 是否有回包）
  - 再确认 `HUAWEI_IOT_MQTT_SCHEME` 是否与 ESP-AT 固件匹配（可通过 `AT+MQTTUSERCFG=?` 验证）

