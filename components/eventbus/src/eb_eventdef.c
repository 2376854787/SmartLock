#include "eb_eventdef.h"

#include "eb_config.h"
#include "eb_event_id.h"

#if (defined(EB_CFG_ENABLE_EVENTMAP) && (EB_CFG_ENABLE_EVENTMAP == 1))
#include "eb_eventmap.h"
#endif

/* 约定：
 * - Edge：drop_policy 必须 EB_DROP_NEW
 * - Snapshot：才允许 EB_OVERWRITE / EB_COALESCE_LATEST
 * - type_tag==0：不做类型检查；否则会在 dispatch 做校验（可配置关闭）
 * - storm_policy：默认 NONE（不启用限频）
 * - prio：强制优先级，eb_publish 会用此值覆盖调用方传入的 prio
 *
 * 字段顺序：
 *   event_id, plane, prio, semantic, drop_policy, type_tag,
 *   storm_policy, storm_min_interval_ms,
 *   storm_tb_capacity, storm_refill_ms, storm_refill_tokens
 */
static const eb_eventdef_t g_defs[] = {
    /* ── SYS ── */
    {EB_EVT_SYS_BOOT, EB_PLANE_CONTROL, EB_PRIO_L, EB_SEM_EDGE, EB_DROP_NEW, 0u, EB_STORM_NONE, 0u,
     0u, 0u, 0u},

    {EB_EVT_SYS_TICK_1MS, EB_PLANE_DATA, EB_PRIO_L, EB_SEM_SNAPSHOT, EB_DROP_NEW, 0u, EB_STORM_NONE,
     0u, 0u, 0u, 0u},
    /* ↑ Data Plane：1kHz 高频，禁止走 EventBus，走 SPSC/RB 直连。
     *   此条目仅作为字典登记（eb_publish 会拒绝 EB_PLANE_DATA）。 */

    {EB_EVT_SYS_HARDFAULT, EB_PLANE_CONTROL, EB_PRIO_H, EB_SEM_EDGE, EB_DROP_NEW, 0u, EB_STORM_NONE,
     0u, 0u, 0u, 0u},

    /* ── BH1750 ── */
    {EB_EVT_BH1750_READY, EB_PLANE_CONTROL, EB_PRIO_M, EB_SEM_SNAPSHOT, EB_OVERWRITE, 0u,
     EB_STORM_NONE, 0u, 0u, 0u, 0u},

    {EB_EVT_BH1750_ERROR, EB_PLANE_CONTROL, EB_PRIO_M, EB_SEM_EDGE, EB_DROP_NEW, 0u, EB_STORM_NONE,
     0u, 0u, 0u, 0u},

    /* ── OLED ── */
    {EB_EVT_OLED_REFRESH_REQ, EB_PLANE_CONTROL, EB_PRIO_L, EB_SEM_SNAPSHOT, EB_COALESCE_LATEST, 0u,
     EB_STORM_NONE, 0u, 0u, 0u, 0u},

    {EB_EVT_OLED_ERROR, EB_PLANE_CONTROL, EB_PRIO_M, EB_SEM_EDGE, EB_DROP_NEW, 0u, EB_STORM_NONE,
     0u, 0u, 0u, 0u},
};
/**
 * @brief 返回定义的事件个数
 * @return 返回当前定义的事件个数
 */
uint32_t eb_eventdef_count(void) {
    return (uint32_t)(sizeof(g_defs) / sizeof(g_defs[0]));
}
/* 启动了O1 哈希查找 */
#if (defined(EB_CFG_ENABLE_EVENTMAP) && (EB_CFG_ENABLE_EVENTMAP == 1))
/**
 * @brief 将当前建立的事件建立哈希表映射
 */
void eb_eventdef_init(void) {
    const uint32_t n = eb_eventdef_count();
    uint32_t ids[sizeof(g_defs) / sizeof(g_defs[0])];
    for (uint32_t i = 0; i < n; i++) {
        ids[i] = g_defs[i].event_id;
    }
    eb_eventmap_build(ids, (uint16_t)n);
}
/**
 * @brief 在哈希过后使用根据事件id查询出对应映射索引
 * @param event_id 事件id
 * @return 返回 -1即获取索引失败
 */
int32_t eb_event_index(uint32_t event_id) {
    return (int32_t)eb_eventmap_lookup(event_id);
}

#else /* eventmap 未启用：线性扫描 fallback */

void eb_eventdef_init(void) {
    /* 无 eventmap：无需初始化 */
}

int32_t eb_event_index(uint32_t event_id) {
    for (uint32_t i = 0; i < eb_eventdef_count(); i++) {
        if (g_defs[i].event_id == event_id) return (int32_t)i;
    }
    return -1;
}

#endif /* EB_CFG_ENABLE_EVENTMAP */
/**
 * @brief 依据事件id返回具体的事件策略
 * @param event_id 事件id
 * @return 返回NULL 即查询失败
 */
const eb_eventdef_t* eb_eventdef_get(uint32_t event_id) {
    const int32_t idx = eb_event_index(event_id);
    if (idx < 0 || idx >= (int32_t)eb_eventdef_count()) return 0;
    return &g_defs[idx];
}

