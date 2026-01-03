# 模块指南：LVGL 集成（显示/触摸/线程模型）

本模块目标：把 LVGL 作为 UI 引擎稳定跑起来，并保证“UI 不会因为外设阻塞而卡死”。

相关路径：

- `Application/Src/lvgl_task.c`
- `Drivers/BSP/lvgl_port/lvgl_port.c`
- 触摸驱动：`Drivers/BSP/touch/gt9xxx.c`、`Drivers/BSP/touch/ctiic.c`
- 触摸映射配置：`components/core_base/config_cus.h`

## 1. 线程模型（必须遵守）

本工程采用“双任务”模型：

- `lvgl_tick`：只负责 `lv_tick_inc()`（时间基准）
- `lvgl_handler`：负责 `lv_timer_handler()`，并且在该线程里初始化 LVGL/显示/触摸/UI

```mermaid
flowchart TD
  A[MX_FREERTOS_Init] --> B[lvgl_init()]
  B --> C[创建 lvgl_mutex]
  C --> D[创建 lvgl_handler]
  C --> E[创建 lvgl_tick]
  D --> D1[lv_init]
  D1 --> D2[lv_port_disp_init]
  D2 --> D3[lv_port_indev_init]
  D3 --> D4[ui_lock_init]
  D4 --> D5[循环: lv_timer_handler]
  E --> E1[循环: lv_tick_inc(5ms)]
```

规则：

1) 只有 `lvgl_handler` 允许直接调用 LVGL API  
2) 其他任务要更新 UI：必须用 `lv_async_call()` 回到 `lvgl_handler` 上下文  
3) 仅在必须跨线程访问 LVGL 时，才用 `lvgl_lock()/lvgl_unlock()`（并尽量短）

## 2. 显示驱动（LVGL → LCD）

`lv_port_disp_init()` 注册 flush 回调，典型流程：

1) LVGL 在 draw buffer 里完成渲染
2) flush_cb 把区域写入 LCD GRAM
3) flush 完成后调用 `lv_disp_flush_ready()`

性能说明：

- 当前实现偏“稳”和“简单”（CPU 循环写 GRAM）
- 若后续需要提升刷新率，可改为 FSMC/DMA，并在 DMA 完成后再 `lv_disp_flush_ready()`

## 3. 触摸驱动（LVGL → GT9xxx）

触摸控制器初始化与读点位于：

- 初始化：`lv_port_indev_init()` → `gt9xxx_init()`（内部使用软 I2C）
- 读点：indev.read_cb → `gt9xxx_read_point()`

软 I2C 引脚定义见 `Drivers/BSP/touch/ctiic.h`（PB0/PF11），电气要求与常见故障见 `docs/hardware.md`。

## 4. 与业务/硬件解耦（为什么 UI 不直接驱动外设）

指纹/刷卡/舵机都可能出现：

- 多轮通信等待（串口/ SPI）
- 等待用户操作（按手指/刷卡）

如果这些逻辑跑在 `lvgl_handler`，UI 会出现“完全卡死”。  
因此工程采用：

- UI 回调 → 投递命令到 worker 队列
- worker 完成 → `lv_async_call()` 回 UI 线程更新显示

指纹/RFID worker 见：`Application/Src/ui_lock.c`  
执行器队列任务见：`Application/Src/lock_actuator.c`

