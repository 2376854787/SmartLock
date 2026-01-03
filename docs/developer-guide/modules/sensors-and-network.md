# 模块指南：传感器与联网任务（概览）

本模块文档描述“当前工程里传感器采集与联网任务”的组织方式与入口，便于论文/联调定位。

## 1. 光敏传感器（Light Sensor）

入口任务：

- `Application/Src/Light_Sensor_task.c:StartLightSensorTask()`

创建点：

- `Core/Src/freertos.c` 创建 `LightSensor_Task`（CMSIS-RTOS2）

注意：

- 当前实现里循环内再次调用 `LightSensor_Init()`，属于偏保守/冗余写法；如果后续优化，可改为只初始化一次
- 日志输出频率为 1s，建议联调 AT/MQTT 时避免刷屏

## 2. 水滴/液体传感器（Water Sensor）

工程存在相关 ADC 配置与任务属性，但当前任务创建在 `Core/Src/freertos.c` 中注释掉：

- `Water_Sensor_TaskHandle = osThreadNew(...)`（注释）

如需启用：

- 恢复任务创建，并实现/接入对应任务入口（以工程实际文件为准）

## 3. 联网与 MQTT（ESP-01S / IoTDA）

联网与 MQTT 的实际运行任务是 `MQTT_AT`（见 `components/AT/AT_Core_Task.c`），入口函数：

- `Application/Src/mqtt_at_task.c:StartMqttAtTask()`

外部协议：

- `docs/mqtt-control.md`

*** Delete File: docs/developer-guide/modules/components-basics.md
