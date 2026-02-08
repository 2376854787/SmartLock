#ifndef SMARTLOCK_EB_EVENTDEF_H
#define SMARTLOCK_EB_EVENTDEF_H
#include <stdbool.h>
#include <stdint.h>

typedef enum { EB_PLANE_CONTROL = 0, EB_PLANE_DATA = 1 } eb_plane_t;
typedef enum { EB_SEM_EDGE = 0, EB_SEM_SNAPSHOT = 1 } eb_semantic_t;
typedef enum { EB_DROP_NEW = 0, EB_OVERWRITE = 1, EB_COALESCE_LATEST = 2 } eb_drop_policy_t;

/* 事件定义 */
typedef struct {
    uint32_t event_id;
    eb_plane_t plane;
    eb_semantic_t semantic;
    eb_drop_policy_t drop_policy; /* Edge 强制 = DROP_NEW；Snapshot 才允许后两者 */
} eb_eventdef_t;

const eb_eventdef_t* eb_eventdef_get(uint32_t event_id);
#endif  // SMARTLOCK_EB_EVENTDEF_H
