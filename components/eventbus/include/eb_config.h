#ifndef SMARTLOCK_EB_CONFIG_H
#define SMARTLOCK_EB_CONFIG_H
#include <stdint.h>

/* ================= 基础容量配置 ================= */
#define EB_CFG_ENABLE_TAP      1 /* 旁路监听器 */
#define EB_CFG_ENABLE_BUDGET   1 /* 记录耗时直方图 */
#define EB_CFG_ENABLE_EVENTMAP 1 /* 哈希查找表 加速事件定义查找 */

#ifndef EB_MAX_EVENTS
#define EB_MAX_EVENTS 128u /* 事件种类数 订阅表尺寸 */
#endif

#ifndef EB_MAX_SUBS_PER_EVENT
#define EB_MAX_SUBS_PER_EVENT 8u /* 单个事件最多允许多少个订阅者 */
#endif

/* 1： 上电时注册所有订阅 eb_freeze()冻结只读 分发不需要锁  0：可以动态增删订阅 */
#ifndef EB_CFG_STATIC_FREEZE
#define EB_CFG_STATIC_FREEZE 1
#endif

/* COW 模式写者锁策略：1=RTOS Mutex（带优先级继承）  0=原子自旋锁（轻量但有优先级反转风险） */
#ifndef EB_COW_USE_MUTEXES
#define EB_COW_USE_MUTEXES 0
#endif

/* 0：禁止分发任务直接调用回调 1：允许 */
#ifndef EB_CFG_ENABLE_CALLBACK
#define EB_CFG_ENABLE_CALLBACK 0
#endif

/* 三队列深度：先用保守值，后续压测再调 */
#ifndef EB_QDEPTH_H
#define EB_QDEPTH_H 32u
#endif
#ifndef EB_QDEPTH_M
#define EB_QDEPTH_M 64u
#endif
#ifndef EB_QDEPTH_L
#define EB_QDEPTH_L 128u
#endif

/* L 配额：每一轮最多处理多少条 L 事件出队处理 */
#ifndef EB_L_QUOTA_PER_ROUND
#define EB_L_QUOTA_PER_ROUND 8u
#endif

/* 统计与断言 */
#ifndef EB_ENABLE_ASSERT
#define EB_ENABLE_ASSERT 1
#endif

/* ================= Top-Tier 运营能力（可裁剪） ================= */

/* 黑盒子记录 1：启用  0：不启用 */
#ifndef EB_CFG_ENABLE_TRACE
#define EB_CFG_ENABLE_TRACE 1
#endif

#ifndef EB_TRACE_DEPTH
#define EB_TRACE_DEPTH 256u /* 追踪环深度：死前 N 条 */
#endif

/* 尝试放到 .noinit linker script 支持；不支持时可关掉） */
#ifndef EB_CFG_TRACE_NOINIT
#define EB_CFG_TRACE_NOINIT 1
#endif

/* 风暴防护 */
#ifndef EB_CFG_ENABLE_STORM
#define EB_CFG_ENABLE_STORM 1
#endif

#ifndef EB_STORM_SLOTS
#define EB_STORM_SLOTS 64u /* (event_id, source_id) 的限频状态槽位 */
#endif

/* Budget Police（p95/p99 统计） */
#ifndef EB_CFG_ENABLE_BUDGET
#define EB_CFG_ENABLE_BUDGET 1
#endif

/* 直方图桶宽：每桶 EB_BUDGET_BUCKET_US 微秒；桶数越大越耗 RAM */
#ifndef EB_BUDGET_BUCKET_US
#define EB_BUDGET_BUCKET_US 50u
#endif
#ifndef EB_BUDGET_BUCKETS
#define EB_BUDGET_BUCKETS 64u
#endif

/* 类型检查 */
#ifndef EB_CFG_ENABLE_TYPECHECK
#define EB_CFG_ENABLE_TYPECHECK 1
#endif

#endif  // SMARTLOCK_EB_CONFIG_H
