#include "eb_api.h"
#include "eb_config.h"
#include "eb_eventdef.h"
#include "eb_port.h"
#include "eb_sub.h"

#if (EB_CFG_ENABLE_TAP == 1)
#include "eb_tap.h"
#endif

#if (EB_CFG_ENABLE_TRACE == 1)
#include "eb_trace.h"
#endif

#ifndef EB_MAX_MATCH
#define EB_MAX_MATCH 16u
#endif

/* 与 eb_core.c 保持一致的 TAP drop reason 编码 */
#define EB_TAP_DROP_NONE      (0u)
#define EB_TAP_DROP_BUSQ_FULL (1u)
#define EB_TAP_DROP_MB_FULL   (2u)
#define EB_TAP_DROP_STORM     (3u)
#define EB_TAP_DROP_TYPE      (4u)

static inline bool eb_filter_hit(const eb_event_t* ev, const eb_sub_t* s) {
    if (s->key_mask == 0u) return true;
    return ((ev->key & s->key_mask) == s->key_value);
}

static inline bool eb_type_ok(const eb_event_t* ev, const eb_sub_t* s) {
#if (EB_CFG_ENABLE_TYPECHECK == 1)
    if (s->expected_type_tag == 0u) return true;
    return (ev->type_tag == s->expected_type_tag);
#else
    (void)ev;
    (void)s;
    return true;
#endif
}

static eb_ret_t dispatch_one(const eb_event_t* ev, const eb_sub_t* s) {
    if (s->delivery == EB_DELIVERY_CALLBACK) {
#if (EB_CFG_ENABLE_CALLBACK == 0)
        return EB_ERR_BADSTATE;
#else
        if (s->cb) {
            s->cb(ev, s->user);
            eb_state_update(CB_CALLED);
        }
        return EB_OK;
#endif
    }

    /* MAILBOX */
    if (!s->mailbox) return EB_ERR_BADARG;

    if (eb_port_mailbox_push(s->mailbox, ev)) {
        eb_state_update(MB_OK);
#if (EB_CFG_ENABLE_TRACE == 1)
        eb_trace_record(EB_TRACE_MB_OK, ev, (eb_drop_reason_t)EB_TAP_DROP_NONE);
#endif
        return EB_OK;
    }

    /* push 失败：尝试 overwrite（仅 Snapshot + OVERWRITE/COALESCE） */
    const eb_eventdef_t* def = eb_eventdef_get(ev->event_id);
    if (def && def->semantic == EB_SEM_SNAPSHOT &&
        (def->drop_policy == EB_OVERWRITE || def->drop_policy == EB_COALESCE_LATEST) &&
        eb_port_mailbox_overwrite(s->mailbox, ev)) {
        eb_state_update(MB_OW_HIT);
#if (EB_CFG_ENABLE_TRACE == 1)
        eb_trace_record(EB_TRACE_MB_OW, ev, (eb_drop_reason_t)EB_TAP_DROP_NONE);
#endif
        return EB_OK;
    }

    eb_state_update(MB_FULL_DROP);
#if (EB_CFG_ENABLE_TRACE == 1)
    eb_trace_record(EB_TRACE_MB_DROP, ev, (eb_drop_reason_t)EB_TAP_DROP_MB_FULL);
#endif
#if (EB_CFG_ENABLE_TAP == 1)
    eb_tap_on_drop(ev, (uint8_t)EB_TAP_DROP_MB_FULL);
#endif
    return EB_ERR_FULL;
}

void eb_dispatch(const eb_event_t* ev) {
#if (EB_CFG_ENABLE_TRACE == 1)
    eb_trace_record(EB_TRACE_DISPATCH, ev, (eb_drop_reason_t)EB_TAP_DROP_NONE);
#endif
#if (EB_CFG_ENABLE_TAP == 1)
    eb_tap_on_dispatch(ev);
#endif

    eb_sub_t list[EB_MAX_MATCH];
    const uint32_t n = eb_sub_find(ev->event_id, list, EB_MAX_MATCH);

    if (n == 0u) {
        eb_state_update(NO_SUB);
        return;
    }

    for (uint32_t i = 0; i < n; i++) {
        if (!eb_filter_hit(ev, &list[i])) {
            eb_state_update(FILT_DROP);
            continue;
        }
        if (!eb_type_ok(ev, &list[i])) {
            eb_state_update(TYPE_MISMATCH);
#if (EB_CFG_ENABLE_TRACE == 1)
            eb_trace_record(EB_TRACE_TYPE_MISMATCH, ev, (eb_drop_reason_t)EB_TAP_DROP_TYPE);
#endif
#if (EB_CFG_ENABLE_TAP == 1)
            eb_tap_on_drop(ev, (uint8_t)EB_TAP_DROP_TYPE);
#endif
            continue;
        }
        (void)dispatch_one(ev, &list[i]);
    }
}
