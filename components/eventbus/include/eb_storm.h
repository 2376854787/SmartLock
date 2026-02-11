#ifndef SMARTLOCK_EB_STORM_H
#define SMARTLOCK_EB_STORM_H

#include <stdbool.h>
#include <stdint.h>

#include "eb_config.h"
#include "eb_eventdef.h"

#if (EB_CFG_ENABLE_STORM == 1)

void eb_storm_init(void);

/* 返回 true 表示允许发布；false 表示被限频丢弃（不产生背压） */
bool eb_storm_allow(const eb_eventdef_t* def, uint16_t source_id, uint32_t now_ms);

#else
static inline void eb_storm_init(void) {}
static inline bool eb_storm_allow(const eb_eventdef_t* def, uint16_t source_id, uint32_t now_ms) {
    (void)def; (void)source_id; (void)now_ms;
    return true;
}
#endif

#endif
