# 模块指南：LVGL 集成（显示/触摸/线程模型）

## 模块职责

- 建立 **LVGL 运行模型**：tick 驱动 + handler 驱动（单线程 UI）。
- 注册 **显示驱动**（LCD flush）与 **输入驱动**（触摸 read）。
- 初始化业务 UI 入口：`ui_lock_init()`。

相关路径：
- `Application/Src/lvgl_task.c`
- `Drivers/BSP/lvgl_port/lvgl_port.c`
- 触摸映射配置：`components/core_base/config_cus.h`

## 核心流程（初始化与循环）

```mermaid
flowchart TD
  A[MX_FREERTOS_Init] --> B[lvgl_init()]
  B --> C[创建互斥锁 lvgl_mutex]
  C --> D[创建 lvgl_handler_task]
  C --> E[创建 lvgl_tick_task]

  D --> D1[lv_init]
  D1 --> D2[lv_port_disp_init]
  D2 --> D3[lv_port_indev_init]
  D3 --> D4[ui_lock_init]
  D4 --> D5[循环：<br/>lv_timer_handler + delay]

  E --> E1[循环：<br/>lv_tick_inc 每 5ms]
```

## 显示与触摸驱动（LVGL→BSP）

```mermaid
flowchart TD
  subgraph LVGL[LVGL 内核]
    L1[Render 到 draw buffer]
    L2[调用 flush_cb(area, color_p)]
    L3[调用 indev.read_cb(data)]
  end

  subgraph BSP[BSP 适配层]
    B1[disp_flush:<br/>lcd_set_window + 写 GRAM]
    B2[touchpad_read:<br/>gt9147_read_point]
  end

  L2 --> B1 --> L1
  L3 --> B2 --> L3
```

## Public API 速查表

| 函数名 | 作用 | 关键参数 | 备注 |
|---|---|---|---|
| `lvgl_init()` | 创建 LVGL 相关任务与互斥锁 | 无 | 由 `MX_FREERTOS_Init()` 调用 |
| `lvgl_lock()` | 获取 LVGL 互斥锁 | 无 | **仅用于必须跨线程访问 LVGL 的场景** |
| `lvgl_unlock()` | 释放 LVGL 互斥锁 | 无 | 与 `lvgl_lock()` 成对使用 |
| `lv_port_disp_init()` | 注册 LVGL 显示驱动 | 无 | flush 回调直接写 LCD GRAM |
| `lv_port_indev_init()` | 注册 LVGL 触摸输入 | 无 | 依赖 GT9xxx/GT9147 驱动 |

## 关键参数（物理含义）

| 配置项 | 位置 | 含义/影响 |
|---|---|---|
| `LVGL_TICK_PERIOD_MS` | `Application/Src/lvgl_task.c` | LVGL tick 周期（ms）；影响动画/定时器精度 |
| `LVGL_HANDLER_TASK_PRIORITY` | `Application/Src/lvgl_task.c` | handler 任务优先级；过低会出现 **“触摸没反应”** |
| `LVGL_TASK_STACK_SIZE` | `Application/Src/lvgl_task.c` | handler 任务栈深度（word）；UI 复杂时需增大 |
| `LVGL_BUF_LINES` | `Drivers/BSP/lvgl_port/lvgl_port.c` | 行缓冲高度；越大刷新越快但更占 RAM |
| `TOUCH_*_INVERT_X/Y` | `components/core_base/config_cus.h` | 触摸方向/镜像修正；排查坐标异常的首要开关 |

## Design Notes（为什么这么写）

- **双任务模型（tick + handler）**：tick 维持 LVGL 时间基准，handler 负责对象定时器/输入处理；拆分后更稳定、时序更可控。
- **flush 用 CPU 循环写 GRAM**：实现简单、可控；后续若刷新性能不足，可改为 FSMC/DMA（需要保证 `lv_disp_flush_ready()` 在 DMA 完成后触发）。
- **触摸坐标映射下沉到触摸驱动**：`lvgl_port` 只读“已修正”的坐标，避免 UI 层关心屏幕方向/模组差异。

