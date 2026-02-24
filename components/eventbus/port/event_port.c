#include <stdbool.h>
#include <string.h>

#include "RingBuffer.h"
#include "assert_cus.h"
#include "eb_config.h"
#include "eb_types.h"
#include "hal_time.h"
#include "osal.h"

#if EB_ENABLE_ASSERT
#define EB_ASSERT_PARAM(x) ASSERT_PARAM((x))
#else
#define EB_ASSERT_PARAM(x) \
    do {                   \
        (void)sizeof(x);   \
    } while (0)
#endif

bool eb_port_mailbox_push(void* mailbox, const eb_event_t* ev) {
    EB_ASSERT_PARAM((mailbox != NULL) && (ev != NULL));
    if ((mailbox == NULL) || (ev == NULL)) return false;
    RingBuffer* rb      = (RingBuffer*)mailbox;
    uint32_t n          = (uint32_t)sizeof(eb_event_t);
    const ret_code_t rc = WriteRingBuffer_SPSC(rb, (const uint8_t*)ev, &n, 0);
    return (rc == RET_OK) && (n == (uint32_t)sizeof(eb_event_t));
}

bool eb_port_mailbox_pop(void* mailbox, eb_event_t* out) {
    EB_ASSERT_PARAM((mailbox != NULL) && (out != NULL));
    if ((mailbox == NULL) || (out == NULL)) return false;
    RingBuffer* rb      = (RingBuffer*)mailbox;
    uint32_t n          = (uint32_t)sizeof(eb_event_t);
    const ret_code_t rc = ReadRingBuffer_SPSC(rb, (uint8_t*)out, &n, 0);
    return (rc == RET_OK) && (n == (uint32_t)sizeof(eb_event_t));
}

void eb_port_enter_critical(uint32_t* state) {
    EB_ASSERT_PARAM(state != NULL);
    if (state == NULL) return;
    OSAL_enter_critical_ex((osal_crit_state_t*)state);
}

void eb_port_exit_critical(uint32_t state) {
    OSAL_exit_critical_ex((osal_crit_state_t)state);
}

/**
 * @brief mailbox overwrite：O(1) 丢弃最旧一条，再写入最新一条
 *
 * 约束：mailbox 只存放定长 eb_event_t；且为 SPSC（BusTask->ModuleTask）。
 */
bool eb_port_mailbox_overwrite(void* mailbox, const eb_event_t* ev) {
    EB_ASSERT_PARAM((mailbox != NULL) && (ev != NULL));
    if (!mailbox || !ev) return false;

    RingBuffer* rb = (RingBuffer*)mailbox;

    /* 1) 先尝试直接写入 */
    uint32_t n     = (uint32_t)sizeof(eb_event_t);
    ret_code_t rc  = WriteRingBuffer_SPSC(rb, (const uint8_t*)ev, &n, 0);
    if ((rc == RET_OK) && (n == (uint32_t)sizeof(eb_event_t))) {
        return true;
    }

    /* 2) 写失败（满）：丢弃最旧一条（零拷贝 drop），再写入 */
    rc = RingBuffer_Drop(rb, (uint32_t)sizeof(eb_event_t), NULL, false);
    if (rc != RET_OK) {
        return false;
    }

    n  = (uint32_t)sizeof(eb_event_t);
    rc = WriteRingBuffer_SPSC(rb, (const uint8_t*)ev, &n, 0);
    return (rc == RET_OK) && (n == (uint32_t)sizeof(eb_event_t));
}

uint32_t eb_port_timestamp(void) {
    return hal_get_tick_ms();
}

// void writer_lock_acquire(void* handle) {
//     OSAL_mutex_lock(*(osal_mutex_t*)handle, OSAL_WAIT_FOREVER);  // 拿不到就挂起（让出 CPU）
// }
// void writer_lock_release(void* handle) {
//     OSAL_mutex_unlock(*(osal_mutex_t*)handle);
// }
