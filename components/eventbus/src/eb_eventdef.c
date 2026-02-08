#include "eb_eventdef.h"

#include "eb_event_id.h"

static const eb_eventdef_t g_defs[] = {
    /* 示例：你按事件表填写 */
    {EB_EVT_SYS_BOOT, EB_PLANE_CONTROL, EB_SEM_EDGE, EB_DROP_NEW},
    {EB_EVT_SYS_HARDFAULT, EB_PLANE_CONTROL, EB_SEM_EDGE, EB_DROP_NEW},

    {EB_EVT_BH1750_READY, EB_PLANE_CONTROL, EB_SEM_SNAPSHOT, EB_OVERWRITE},
    {EB_EVT_BH1750_ERROR, EB_PLANE_CONTROL, EB_SEM_EDGE, EB_DROP_NEW},

    {EB_EVT_OLED_REFRESH_REQ, EB_PLANE_CONTROL, EB_SEM_SNAPSHOT, EB_COALESCE_LATEST},
    {EB_EVT_OLED_ERROR, EB_PLANE_CONTROL, EB_SEM_EDGE, EB_DROP_NEW},
};

const eb_eventdef_t* eb_eventdef_get(uint32_t event_id) {
    for (uint32_t i = 0; i < (uint32_t)(sizeof(g_defs) / sizeof(g_defs[0])); i++) {
        if (g_defs[i].event_id == event_id) return &g_defs[i];
    }
    return 0;
}
