#ifndef SMARTLOCK_EB_TYPES_H
#define SMARTLOCK_EB_TYPES_H
#include <stdbool.h>
#include <stdint.h>

#include "ret_code.h"
/**============================================================================================ */
/**==================================       发布使用          =================================== */
/**============================================================================================ */
#define RET_EB_MAKE(__cla, __rea) \
    RET_MAKE((RET_MOD_EVENTBUS), (RET_SUB_EVENTBUS_SYSTEM), RET_CODE_MAKE((__cla), (__rea)))
typedef enum {
    EB_PRIO_H = 0,
    EB_PRIO_M = 1,
    EB_PRIO_L = 2,
} eb_prio_t;

/* 最小事件：后续再扩展 payload 合同 A/B/C */
typedef struct {
    uint32_t event_id;
    eb_prio_t prio;
    uint32_t key;         /* 给后续 filter 用 */
    uint32_t payload_u32; /* 先按值传，避免指针所有权地狱 */
    uint32_t ts;          /* timestamp：port 提供 */
} eb_event_t;

_Static_assert(sizeof(eb_event_t) <= 20, "eb_event_t 对于邮箱太大了");
_Static_assert((sizeof(eb_event_t) % 4) == 0, "参考4字节对齐");
typedef enum {
    EB_OK         = RET_OK,
    EB_ERR_FULL   = RET_EB_MAKE(RET_CLASS_RESOURCE, RET_R_QUEUE_FULL),
    EB_ERR_BADARG = RET_EB_MAKE(RET_CLASS_PARAM, RET_R_INVALID_ARG),
} eb_ret_t;
/**============================================================================================ */
/**==================================       订阅使用          =================================== */
/**============================================================================================ */
typedef void (*eb_cb_t)(const eb_event_t* ev, void* user);
/* 投递的类型 */
typedef enum {
    EB_DELIVERY_CALLBACK = 0, /* 直接调用回调函数 */
    EB_DELIVERY_MAILBOX  = 1, /* 投递到指定模块的 邮箱 */
} eb_delivery_t;

typedef struct {
    uint32_t event_id;      /* 事件 ID */
    eb_delivery_t delivery; /* 投递方式的选择 */
    eb_cb_t cb;             /* 回调函数 */
    void* mailbox;          /* 投递到接受者的 邮箱 */
    void* user;             /* user 指针给 callback */

    uint32_t key_mask;  /* 配合 事件成员key使用 0 表示不过滤 */
    uint32_t key_value; /* (event.key & mask) == value 才投递 */
} eb_sub_t;

#endif  // SMARTLOCK_EB_TYPES_H
