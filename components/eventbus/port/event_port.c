#include <stdbool.h>
#include <stddef.h> /* NULL */

#include "RingBufferTyped.h"
#include "assert_cus.h"
#include "eb_config.h"
#include "eb_types.h"
#include "hal_time.h"
#include "osal.h"

/* mailbox 句柄是一个定长元素队列，每个元素是一条 eb_event_t。元素大小在创建
 * mailbox 时就交给 TypedRB 记住，这里不再手写 sizeof(eb_event_t)。
 *
 * 并发模型：统一用带锁。BusTask（生产者）顺序调用 push / 失败再 overwrite，
 * ModuleTask（消费者）调用 pop。overwrite 满时要丢最旧（动 front），不能走纯
 * SPSC，所以整个 mailbox 统一带锁，避免「一半 SPSC 一半带锁」的混用竞争。 */

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
    return TypedRB_Push((TypedRB*)mailbox, ev);
}

bool eb_port_mailbox_pop(void* mailbox, eb_event_t* out) {
    EB_ASSERT_PARAM((mailbox != NULL) && (out != NULL));
    if ((mailbox == NULL) || (out == NULL)) return false;
    return TypedRB_Pop((TypedRB*)mailbox, out);
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
 * @brief mailbox overwrite：满了就丢掉最旧一条，再写入最新一条
 *
 * 约束：mailbox 只存放定长 eb_event_t。「丢最旧再写」要同时动 front/rear，
 * 走带锁版；mailbox 的 push/pop 也是带锁，模型一致。
 */
bool eb_port_mailbox_overwrite(void* mailbox, const eb_event_t* ev) {
    EB_ASSERT_PARAM((mailbox != NULL) && (ev != NULL));
    if (!mailbox || !ev) return false;
    return TypedRB_PushOverwriteOldest((TypedRB*)mailbox, ev);
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
