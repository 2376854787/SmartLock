# EventBus Test Matrix

> **Version**: v3.0 (Top-Tier Event System)
> **Platform**: STM32F407ZG / FreeRTOS
> **Last Updated**: 2026-02-11

---

## 1. 功能验收 (Functional Acceptance)

### 1.1 发布与订阅

| ID | 测试项 | 步骤 | 预期结果 | 状态 |
|:--|---|---|---|:--:|
| F-01 | 基本发布-订阅 | 注册 1 个订阅者 → 发布 1 条 Control 事件 | 订阅者 mailbox 收到事件，字段完整 | [ ] |
| F-02 | 多订阅者广播 | 注册 N（≥3）个订阅者 → 发布 1 条事件 | 所有订阅者均收到事件 | [ ] |
| F-03 | Data Plane 拒绝 | 发布 `EB_PLANE_DATA` 事件 | `eb_publish()` 返回 `EB_ERR_BADARG` | [ ] |
| F-04 | 未注册事件拒绝 | 发布未在 `g_defs[]` 注册的 event_id | `eb_publish()` 返回 `EB_ERR_BADARG` | [ ] |
| F-05 | 幂等订阅 | 同一 `eb_sub_t` 注册两次 | 第二次返回 `EB_OK`，订阅者列表去重 | [ ] |
| F-06 | Freeze 后拒绝订阅 | `eb_freeze()` 后调用 `eb_sub_add()` | 返回 `EB_ERR_BADSTATE` | [ ] |
| F-07 | Prio 强制覆盖 | 发布时填 `prio=L`，eventdef 定义为 `H` | 实际入 H 队列 | [ ] |

### 1.2 优先级调度

| ID | 测试项 | 步骤 | 预期结果 | 状态 |
|:--|---|---|---|:--:|
| P-01 | H 优先于 M/L | 同时入 H×1 + M×1 + L×1，调用 `eb_pump_once()` | H 最先出队处理 | [ ] |
| P-02 | L 配额限制 | 入 L×(2×EB_L_QUOTA)，单次 `pump_once` | 仅处理 EB_L_QUOTA_PER_ROUND 条 L | [ ] |
| P-03 | M 全排空再 L | 入 M×5 + L×5，pump | M 全部先处理，然后 L 最多 quota 条 | [ ] |

### 1.3 前置过滤

| ID | 测试项 | 步骤 | 预期结果 | 状态 |
|:--|---|---|---|:--:|
| FL-01 | mask/value 命中 | 订阅者 mask=0xFF, value=0x42 → 发布 key=0x42 | 投递成功 | [ ] |
| FL-02 | mask/value 未中 | 同上 → 发布 key=0x43 | 不投递，`FILT_DROP++` | [ ] |
| FL-03 | mask=0 全通 | 订阅者 mask=0 → 发布任意 key | 全部投递 | [ ] |

### 1.4 类型安全

| ID | 测试项 | 步骤 | 预期结果 | 状态 |
|:--|---|---|---|:--:|
| T-01 | type_tag 匹配 | 发布 tag=0x1001，订阅者 expected=0x1001 | 正常投递 | [ ] |
| T-02 | type_tag 不匹配 | 发布 tag=0x1001，订阅者 expected=0x2002 | 不投递，`TYPE_MISMATCH++`，trace 记录 | [ ] |
| T-03 | tag=0 不检查 | 订阅者 expected=0 | 任何 tag 均通过 | [ ] |

### 1.5 智能丢弃 (Smart Drop)

| ID | 测试项 | 步骤 | 预期结果 | 状态 |
|:--|---|---|---|:--:|
| D-01 | Edge 队列满丢弃 | 填满 H 队列 → 再入 1 条 Edge/H | 返回 `EB_ERR_FULL`，`DROP_Q_H++` | [ ] |
| D-02 | Snapshot 覆盖 | 填满 L 队列 → 再入 1 条 Snapshot/OVERWRITE | 覆盖成功，`OW_Q_HIT++` | [ ] |
| D-03 | Mailbox 满覆盖 | Snapshot 事件，mailbox 满 | `eb_port_mailbox_overwrite` 成功，`MB_OW_HIT++` | [ ] |
| D-04 | Mailbox 满丢弃 (Edge) | Edge 事件，mailbox 满 | 丢弃，`MB_FULL_DROP++`，trace `MB_DROP` | [ ] |

### 1.6 Typed API 宏

| ID | 测试项 | 步骤 | 预期结果 | 状态 |
|:--|---|---|---|:--:|
| TA-01 | `eb_publish_typed` | 使用宏发布，检查 type_tag 和 返回值 | 事件正确入队，rc==EB_OK | [ ] |
| TA-02 | `eb_subscribe_typed` | 使用宏订阅，检查 expected_type_tag | 订阅成功，tag 匹配时可投递 | [ ] |

---

## 2. 实时性验收 (Latency Verification)

| ID | 测试项 | 步骤 | 预期结果 | KPI |
|:--|---|---|---|---|
| RT-01 | H 延迟上界 | L 洪水 1000 条 + 插入 H 急停 1 条 | H 在 BusTask 下一轮即处理 | H p99 < 配置上界 |
| RT-02 | Budget 直方图有效 | 正常运行 1000 轮 pump | `eb_budget_query_round()` 返回非零 p50/p95/p99 | 数据合理 |
| RT-03 | 50 订阅者广播 | 1 个事件注册 50 个订阅者 → 发布 | Budget Police 记录可解释的 p95/p99 | p99 < 阈值 |

> [!NOTE]
> RT-01 和 RT-03 的精确测量需要 DWT 级 `eb_port_timestamp_us()` 实现（当前为 TODO）。在此之前，可通过 GPIO toggle + 逻辑分析仪验证。

---

## 3. Storm Protection 验收

| ID | 测试项 | 步骤 | 预期结果 | 状态 |
|:--|---|---|---|:--:|
| ST-01 | MIN_INTERVAL 限频 | 配置 min_interval=100ms，10ms 内发布 10 条 | 仅第 1 条通过，其余 `STORM_DROP++` | [ ] |
| ST-02 | TOKEN_BUCKET 限频 | 容量=5，refill=1000ms/1token → 连发 10 条 | 前 5 条通过，后 5 条丢弃 | [ ] |
| ST-03 | H Edge 默认不限频 | H Edge 事件，storm_policy=NONE | 不管频率，全部通过 | [ ] |
| ST-04 | 竞争退化 | 双线程同时 storm_allow 同一 slot | 竞争时退化为放行（不阻塞） | [ ] |

---

## 4. Flight Recorder 验收

| ID | 测试项 | 步骤 | 预期结果 | 状态 |
|:--|---|---|---|:--:|
| FR-01 | 热重启恢复 | 发布 N 条 → 软件 reset → 启动读取 trace | header.magic 有效，entries 包含死前事件 | [ ] |
| FR-02 | WDG 恢复 | 触发看门狗 timeout → 重启读取 | reset_reason=WDG，trace 完整 | [ ] |
| FR-03 | 冷启动清空 | 断电重启 → 读取 trace | header 检测到不可信，清空 entries | [ ] |
| FR-04 | Commit 完整性 | 发布后立即硬 reset（模拟） | 未提交（commit!=MAGIC）条目被忽略 | [ ] |
| FR-05 | 多生产者无撕裂 | 多任务并发 trace_record | 所有 committed 条目 seq 连续、字段完整 | [ ] |

---

## 5. Ops 增强验收

### 5.1 Promiscuous Tap

| ID | 测试项 | 步骤 | 预期结果 | 状态 |
|:--|---|---|---|:--:|
| TAP-01 | 基本监听 | 注册 sink + enable_mask=0x7 → 发布+分发+丢弃 | sink 均被回调 | [ ] |
| TAP-02 | 采样率 | sample_pow2=2 → 发布 16 条 | sink 被调用 4 次 | [ ] |
| TAP-03 | 默认关闭 | 不调用 eb_tap_init | 无 sink 调用 | [ ] |

### 5.2 Budget Police

| ID | 测试项 | 步骤 | 预期结果 | 状态 |
|:--|---|---|---|:--:|
| BP-01 | 直方图记录 | 运行 100 轮 pump → 查询 hist | 桶计数 > 0 | [ ] |
| BP-02 | Reset 清零 | `eb_budget_reset()` → 查询 | 全部为 0 | [ ] |
| BP-03 | Max 更新 | 人为延长单事件处理 → 查询 max | max_us > 0 且合理 | [ ] |

---

## 6. 故障注入 (Fault Injection)

| ID | 测试项 | 步骤 | 预期结果 | 状态 |
|:--|---|---|---|:--:|
| FI-01 | 队列满洪水 | 持续发布直到 H/M/L 全满 | 返回 EB_ERR_FULL，计数器正确，trace 记录 DROP | [ ] |
| FI-02 | Mailbox 满洪水 | 订阅者不消费 → 持续发布 | MB_FULL_DROP/MB_OW_HIT 正确计数 | [ ] |
| FI-03 | Storm 表满 | 用完 EB_STORM_SLOTS → 再来新 (eid,sid) | 退化为不限频（放行），不崩溃 | [ ] |
| FI-04 | HardFault 恢复 | 触发 HardFault → 重启 | Flight Recorder 留有证据链 | [ ] |
| FI-05 | NULL 指针防御 | 传入 NULL 给所有 public API | 返回错误码，不崩溃 | [ ] |

---

## 7. EventMap (O(1) 查找) 验收

| ID | 测试项 | 步骤 | 预期结果 | 状态 |
|:--|---|---|---|:--:|
| EM-01 | 查找已注册 ID | 初始化后 lookup 所有 g_defs 事件 | 每个返回正确 def_index | [ ] |
| EM-02 | 查找未注册 ID | lookup 一个不存在的 event_id | 返回 -1 | [ ] |
| EM-03 | get 返回正确指针 | `eb_eventdef_get()` 对每个已注册 ID | 返回非 NULL 且字段匹配 | [ ] |

---

## 统计总览

| 类别 | 用例数 | 通过 | 失败 | 待测 |
|---|:--:|:--:|:--:|:--:|
| 功能验收 | 21 | 0 | 0 | 21 |
| 实时性验收 | 3 | 0 | 0 | 3 |
| Storm | 4 | 0 | 0 | 4 |
| Flight Recorder | 5 | 0 | 0 | 5 |
| Ops 增强 | 6 | 0 | 0 | 6 |
| 故障注入 | 5 | 0 | 0 | 5 |
| EventMap | 3 | 0 | 0 | 3 |
| **合计** | **47** | **0** | **0** | **47** |
