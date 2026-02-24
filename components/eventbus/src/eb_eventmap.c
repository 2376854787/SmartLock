#include "eb_eventmap.h"
/* 检查是否启用了 事件映射 */
#if (defined(EB_CFG_ENABLE_EVENTMAP) && (EB_CFG_ENABLE_EVENTMAP == 1))
#include <string.h>

#include "assert_cus.h"
#include "eb_config.h"

#if EB_ENABLE_ASSERT
#define EB_ASSERT_PARAM(x) ASSERT_PARAM((x))
#else
#define EB_ASSERT_PARAM(x) \
    do {                   \
        (void)sizeof(x);   \
    } while (0)
#endif
#ifndef EB_EVENTMAP_CAP
#define EB_EVENTMAP_CAP 256 /* 建议>= 事件数*2  */
#endif

/* 哈希表 */
static eb_eventmap_slot_t g_map[EB_EVENTMAP_CAP];
/**
 * @brief 计算事件id哈希值
 * @param x 事件id
 * @return
 */
static inline uint32_t h32(uint32_t x) {
    /* 高16位混入低16位 */
    x ^= x >> 16;
    /* 精心的质数 */
    x *= 0x7feb352dU;
    /* 再次混合 */
    x ^= x >> 15;
    /* 再次乘以质数 */
    x *= 0x846ca68bU;
    /* 再次混合 */
    x ^= x >> 16;
    return x;
}
/**
 * @brief 将事件策略表建立哈希映射
 * @param event_ids 事件表
 * @param n 事件个数
 */
void eb_eventmap_build(const uint32_t* event_ids, uint16_t n) {
    /* 容器初始化置零 */
    memset(g_map, 0, sizeof(g_map));
    /* 参数检查 */
    EB_ASSERT_PARAM((event_ids != NULL) || (n == 0u));
    if (!event_ids) return;
    /* 遍历事件表建立哈希映射 */
    for (uint16_t i = 0; i < n; i++) {
        const uint32_t id = event_ids[i];
        uint32_t idx      = h32(id) % EB_EVENTMAP_CAP;
        for (uint32_t probe = 0; probe < EB_EVENTMAP_CAP; probe++) {
            eb_eventmap_slot_t* s = &g_map[idx];
            if (!s->used) {
                s->used      = 1;
                s->event_id  = id;
                s->def_index = i;
                break;
            }
            idx = (idx + 1u) % EB_EVENTMAP_CAP;
        }
    }
}
/**
 * @brief 返回该事件在 g_defs 里的索引
 * @param event_id 事件id
 * @return 返回该事件在 g_defs 里的索引
 */
int eb_eventmap_lookup(uint32_t event_id) {
    uint32_t idx = h32(event_id) % EB_EVENTMAP_CAP;
    for (uint32_t probe = 0; probe < EB_EVENTMAP_CAP; probe++) {
        const eb_eventmap_slot_t* s = &g_map[idx];
        if (!s->used) return -1;
        /* 找到了直接返回 */
        if (s->event_id == event_id) return (int)s->def_index;
        /* 没有找到继续下一个 */
        idx = (idx + 1u) % EB_EVENTMAP_CAP;
    }
    return -1;
}

#endif

