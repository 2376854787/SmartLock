#ifndef EB_EVENTMAP_H
#define EB_EVENTMAP_H

#include <stdbool.h>
#include <stdint.h>

#include "eb_config.h"

#if (EB_CFG_ENABLE_EVENTMAP == 1)

/* 事件索引映射：把 event_id -> def_index 变成 O(1)
 * 方案：稠密 ID 用 direct map；否则用 open addressing 哈希表
 */

typedef struct {
    uint32_t event_id;  /* 事件id */
    uint16_t def_index; /* 这个事件在 g_defs[] 的索引 */
    uint16_t used;      /* 0/1 */
} eb_eventmap_slot_t;

void eb_eventmap_build(const uint32_t* event_ids, uint16_t n);
int eb_eventmap_lookup(uint32_t event_id); /* 返回 def_index，找不到返回 -1 */

#endif
#endif
