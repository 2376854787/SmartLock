#ifndef SMARTLOCK_EB_PORT_H
#define SMARTLOCK_EB_PORT_H
#include <stdbool.h>
#include <stdint.h>

#include "eb_types.h"

/* 将事件投递到 mailbox，成功返回 true，失败（满）返回 false */
bool eb_port_mailbox_push(void* mailbox, const eb_event_t* ev);
bool eb_port_mailbox_pop(void* mailbox, eb_event_t* out);  // 模块Task用

/* 进入/退出临界区（允许 ISR/Task 调用；实现应最短） */
void eb_port_enter_critical(uint32_t* state);
void eb_port_exit_critical(uint32_t state);

/* 时间戳：ms 单调递增（必须可用） */
uint32_t eb_port_timestamp(void);

/* 时间戳：us 单调递增（可选；不提供则默认 ms*1000） */
uint32_t eb_port_timestamp_us(void);

/* 在 mailbox 内覆盖（Snapshot 允许）：O(1) 丢弃最旧一条再写最新 */
bool eb_port_mailbox_overwrite(void* mailbox, const eb_event_t* ev);
void writer_lock_acquire(void* handle);
void writer_lock_release(void* handle);
/* reset 原因（可选；用于 Flight Recorder header）
 * 如果平台未提供，默认返回 0（unknown）
 */
typedef enum {
    EB_RESET_UNKNOWN = 0,
    EB_RESET_POR     = 1,
    EB_RESET_PIN     = 2,
    EB_RESET_WDG     = 3,
    EB_RESET_SOFT    = 4,
    EB_RESET_BOR     = 5,
    EB_RESET_OTHER   = 255,
} eb_reset_reason_t;

eb_reset_reason_t eb_port_read_reset_reason_and_clear(void);

#endif  // SMARTLOCK_EB_PORT_H
