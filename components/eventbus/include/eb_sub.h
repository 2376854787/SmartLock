#ifndef SMARTLOCK_EB_SUB_H
#define SMARTLOCK_EB_SUB_H
#pragma once
#include "eb_types.h"
/* 最大订阅数 */
#ifndef EB_MAX_SUBS
#define EB_MAX_SUBS 64u
#endif

void eb_sub_init(void);

/* 注册订阅：成功返回 EB_OK，满了返回 EB_ERR_FULL */
eb_ret_t eb_sub_add(const eb_sub_t* s);

/* 可选：取消订阅（先做精确匹配删除） */
eb_ret_t eb_sub_remove(const eb_sub_t* s);

/* 内部：按 event_id 查订阅者 */
uint32_t eb_sub_find(uint32_t event_id, const eb_sub_t** out_list, uint32_t max);

#endif  // SMARTLOCK_EB_SUB_H
