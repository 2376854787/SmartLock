#ifndef SMARTLOCK_EB_TYPES_H
#define SMARTLOCK_EB_TYPES_H
#include <stdbool.h>
#include <stdint.h>

#include "ret_code.h"

/**============================================================================================ */
/**==================================       发布使用          =================================== */
/**============================================================================================ */

#define RET_EB_MAKE(__cla, __rea)     RET_MAKE((RET_MOD_EVENTBUS), (RET_SUB_EVENTBUS_SYSTEM), RET_CODE_MAKE((__cla), (__rea)))

typedef enum {
    EB_PRIO_H = 0,
    EB_PRIO_M = 1,
    EB_PRIO_L = 2,
} eb_prio_t;

/*
 * 事件载荷合同（当前版本：固定 1x u32）
 *
 * Top-Tier 要求：
 * - 必须能携带 source_id（Storm/追踪用）
 * - 必须能携带 type_tag（Typed API / 防呆）
 */
typedef struct {
    uint32_t event_id;
    eb_prio_t prio;

    uint32_t key;         /* filter 用：(key & mask) == value */
    uint32_t payload_u32; /* 当前版本固定载荷 */

    uint16_t source_id;   /* 发布源：模块/设备/通道；Storm 以 (event_id,source_id) 限频 */
    uint16_t type_tag;    /* 载荷类型指纹：0 表示不做类型约束 */

    uint32_t ts;          /* timestamp：port 提供（ms 单调递增） */
} eb_event_t;

#define EB_EVENT_SIZE (sizeof(eb_event_t))
_Static_assert((sizeof(eb_event_t) % 4) == 0, "eb_event_t 需 4 字节对齐");
_Static_assert(sizeof(eb_event_t) <= 32, "eb_event_t 过大：请检查字段膨胀");

/* 返回码映射 */
typedef enum {
    EB_OK           = RET_OK,
    EB_ERR_FULL     = RET_EB_MAKE(RET_CLASS_RESOURCE, RET_R_QUEUE_FULL),
    EB_ERR_BADARG   = RET_EB_MAKE(RET_CLASS_PARAM, RET_R_INVALID_ARG),
    EB_ERR_BADSTATE = RET_EB_MAKE(RET_CLASS_STATE, RET_R_STATE_ERR),
} eb_ret_t;

/**============================================================================================ */
/**==================================       订阅使用          =================================== */
/**============================================================================================ */

typedef void (*eb_cb_t)(const eb_event_t* ev, void* user);

/* 投递类型 */
typedef enum {
    EB_DELIVERY_CALLBACK = 0, /* 直接调用回调函数 */
    EB_DELIVERY_MAILBOX  = 1, /* 投递到指定模块的 mailbox */
} eb_delivery_t;

typedef struct {
    uint32_t event_id;      /* 事件 ID */
    eb_delivery_t delivery; /* 投递方式 */
    eb_cb_t cb;             /* 回调 */
    void* mailbox;          /* mailbox（delivery=MAILBOX 时必须非空） */
    void* user;             /* user 指针给 callback */

    uint32_t key_mask;      /* 0 表示不过滤 */
    uint32_t key_value;     /* (event.key & mask) == value 才投递 */

    /* Typed API：0 表示不检查；否则要求 ev.type_tag == expected_type_tag */
    uint16_t expected_type_tag;
    uint16_t _reserved0;
} eb_sub_t;

#endif  // SMARTLOCK_EB_TYPES_H
