# 模块指南：RC522 RFID（MFRC522）

本模块提供 MFRC522 的初始化与基础操作（寻卡/防冲突/选卡等），业务层用于：

- 读取 UID
- 与 `lock_data` 的白名单进行校验
- 录入/删除卡片（UI 管理页）

相关路径：

- 驱动：`Drivers/BSP/rc522/rc522_my.h`、`Drivers/BSP/rc522/rc522_my.c`
- UI 集成：`Application/Src/ui_lock.c`（rfid worker）
- 初始化任务：`Application/Src/lock_devices.c`（`dev_init`）

## 1. 硬件连接（以 CubeMX 为准）

- 总线：SPI1
- 片选：PG6（`Rc522_CS_Pin`）
- NRT/IRQ：PG7（`Rc522_NRT_Pin`，以硬件接法为准）

## 2. 初始化与 ready 机制（工程约定）

RC522 初始化不在 UI 线程做，而在 `dev_init` 任务中做：

- 启动点：`Core/Src/freertos.c` 调用 `LockDevices_Start()`
- 初始化任务：`Application/Src/lock_devices.c:dev_init_task()`
  - `RC522_Init()`
  - 读取 `VersionReg` 做存活检查（`0x00/0xFF` 通常表示 SPI/片选异常）

UI/worker 使用前建议：

- 等待：`LockDevices_WaitRc522Ready(timeout_ms)`
- 或检查：`LockDevices_Rc522Ready()`

## 3. 典型“寻卡 → 读 UID”流程

```mermaid
flowchart TD
  A[RC522 已初始化] --> B[循环]
  B --> C[PcdRequest(PICC_REQALL)]
  C -->|找到卡| D[PcdAnticoll 读取 UID]
  D --> E[PcdSelect 选卡]
  E --> F[交给业务层<br/>lock_rfid_verify_uid / lock_rfid_add_uid]
  C -->|未找到| B
```

## 4. 常用 API（驱动层）

| API | 作用 |
|---|---|
| `RC522_Init()` | 初始化 RC522 |
| `PcdRequest(req_code, pTagType)` | 寻卡 |
| `PcdAnticoll(pSnr)` | 防冲突并读 UID |
| `PcdSelect(pSnr)` | 选卡 |
| `PcdHalt()` | 休眠卡片 |

## 5. 关键实现注意（SPI 发送 dummy byte）

SPI 读写时，必须传入有效的发送缓冲（dummy byte），否则会造成不可预期行为。  
本工程在 `Drivers/BSP/rc522/rc522_my.c` 中已将“非法指针作为 TX”修正为真实 dummy 字节发送。

## 6. 常见故障（优先排查）

- `VersionReg=0x00/0xFF`：片选/连线/SPI 模式/供电问题
- 扫卡不稳定：RC522 供电与天线布局、地线回路、SPI 线长
- UI 卡死：不要在 `lvgl_handler` 里做寻卡循环，必须在 worker 做

*** Delete File: docs/developer-guide/modules/cloud-huawei-iotda.md
