# 模块指南：门锁业务数据（RFID / PIN）

## 模块职责

- 提供一个 **线程安全的内存态凭据库**：
  - RFID 白名单（UID + 名称 + 启用状态）
  - PIN/密码（数字串 + TTL + 启用状态）
- 提供 **观察者回调**：数据变更时通知 UI 刷新。

相关路径：
- `Application/Inc/lock_data.h`
- `Application/Src/lock_data.c`

## 核心流程（以“增删改→通知”为主）

```mermaid
flowchart TD
  A[调用 lock_* API] --> B[确保 mutex 已创建]
  B --> C[加锁 xSemaphoreTake]
  C --> D[读写内存数组<br/>s_rfid / s_pins]
  D --> E[解锁 xSemaphoreGive]
  E --> F{是否注册 observer?}
  F -->|是| G[回调 lock_observer_t(evt)]
  F -->|否| H[结束]
```

## Public API 速查表

| 函数名 | 作用 | 关键参数 | 备注 |
|---|---|---|---|
| `lock_data_init()` | 初始化内部互斥锁 | 无 | 可重复调用 |
| `lock_data_set_observer()` | 注册数据变更通知 | `cb`, `user_ctx` | UI 可用它触发列表刷新 |
| `lock_time_now_s()` | 返回“上电后秒数” | 无 | 基于 `xTaskGetTickCount()`（非 RTC） |
| `lock_rfid_add_uid()` | 添加 UID | `uid[4]`, `name_opt` | 若已存在则返回 true（幂等） |
| `lock_rfid_remove_uid()` | 删除 UID | `uid[4]` | 找不到返回 false |
| `lock_rfid_clear()` | 清空 RFID | 无 | 会触发 `LOCK_EVT_RFID_CHANGED` |
| `lock_pin_add()` | 添加 PIN | `pin_digits`, `ttl_minutes`, `name_opt`, `out_id` | **只允许数字**，超长/非法返回 false |
| `lock_pin_verify()` | 校验 PIN | `pin_digits`, `out_id` | 会检查 TTL/启用状态 |
| `lock_pin_remove()` | 删除 PIN（按 id） | `id` | `id` 为稳定 CRUD 标识 |
| `lock_pin_clear()` | 清空 PIN 库 | 无 | 会触发 `LOCK_EVT_PIN_CHANGED` |

## 关键参数（物理含义）

| 配置项 | 位置 | 含义/影响 |
|---|---|---|
| `LOCK_RFID_UID_LEN` | `Application/Inc/lock_data.h` | UID 长度（当前按 RC522 常见 4 字节 UID） |
| `LOCK_RFID_MAX` | `Application/Inc/lock_data.h` | RFID 最大条目数（RAM 占用与容量上限） |
| `LOCK_PIN_MAX_LEN` | `Application/Inc/lock_data.h` | PIN 最大位数（当前 6） |
| `LOCK_PIN_MAX` | `Application/Inc/lock_data.h` | PIN 最大条目数 |

## Design Notes（为什么这么写）

- **内部 mutex 保证线程安全**：UI、worker、未来 MQTT 同步都可能访问同一份数据；使用互斥锁能避免“半写入/越界/计数错误”。
- **observer 解耦 UI 刷新**：数据层不直接依赖 LVGL，UI 决定如何刷新与呈现。
- **时间基准先用 tick 秒**：开发阶段不依赖 RTC/NTP；未来替换为真实 epoch 时，仅需替换 `lock_time_now_s()` 的实现即可。

