# 模块指南：RC522 RFID（MFRC522）

## 模块职责

- 提供 RC522 的初始化与基础操作（寻卡/防冲突/选卡等）。
- 在本项目中主要被 UI/测试任务用于 **读取 UID 并写入/校验 `lock_data`**。

相关路径：
- `Drivers/BSP/rc522/rc522_my.h`
- `Drivers/BSP/rc522/rc522_my.c`
- 测试任务：`Application/Src/rc522_my_test_task.c`
- UI 集成：`Application/Src/ui_lock.c`

## 典型“寻卡→读 UID”流程

```mermaid
flowchart TD
  A[RC522_Init] --> B[循环]
  B --> C[PcdRequest(PICC_REQALL)]
  C -->|找到卡| D[PcdAnticoll 读取 UID]
  D --> E[PcdSelect 选卡]
  E --> F[把 UID 交给业务<br/>lock_rfid_add_uid/verify]
  C -->|未找到| B
```

## Public API 速查表（常用）

| 函数名 | 作用 | 关键参数 | 备注 |
|---|---|---|---|
| `RC522_Init()` | 初始化 RC522 | 无 | 依赖 SPI 与片选引脚配置 |
| `PcdRequest()` | 寻卡 | `req_code`, `pTagType` | 常用 `PICC_REQALL` |
| `PcdAnticoll()` | 防冲突并读 UID | `pSnr` | 输出 UID（本项目按 4 字节用） |
| `PcdSelect()` | 选卡 | `pSnr` | 读到 UID 后调用 |
| `PcdHalt()` | 休眠卡片 | 无 | 可用于降低重复触发 |

## 关键参数（物理含义）

| 配置项 | 位置 | 含义 |
|---|---|---|
| `PICC_REQALL` | `Drivers/BSP/rc522/rc522_my.h` | “寻所有卡”命令码 |
| `DEF_FIFO_LENGTH` | `Drivers/BSP/rc522/rc522_my.h` | RC522 FIFO 长度（硬件固定 64 字节） |

## Design Notes（为什么这么写）

- **基础驱动保持“薄”**：RC522 层只提供寻卡/选卡等原语，业务层（UI/lock_data）决定“是否加入白名单/如何命名/如何提示 UI”。
- **与 UI 解耦**：读到 UID 后只需向上抛出数据，UI 再决定显示与存储策略。

