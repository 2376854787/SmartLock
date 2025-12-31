#ifndef SMARTLOCK_OSAL_H
#define SMARTLOCK_OSAL_H

#include <stdint.h>
#include <stdbool.h>
#include "ret_code.h"


#ifdef __cplusplus
extern "C" {



#endif
/* =========== 通用常量 ========== */
#define OSAL_WAIT_FOREVER (0xFFFFFFFFU)
/* =========================================== 基础类型（自定义实现解耦） =========================================== */
typedef uint32_t osal_tick_t;
typedef uint32_t osal_crit_state_t;

typedef void *osal_mutex_t;
typedef void *osal_sem_t;
typedef void *osal_msgq_t;
typedef void *osal_thread_t;

typedef uint32_t osal_flags_t;

/* 线程入口函数 */
typedef void (*osal_thread_fn_t)(void *arg);

typedef enum {
    OSAL_FLAGS_WAIT_ANY = 0,
    OSAL_FLAGS_WAIT_ALL = 1
} osal_flags_wait_t;

typedef enum {
    OSAL_PRIO_LOW = 0,
    OSAL_PRIO_NORMAL,
    OSAL_PRIO_HIGH,
    OSAL_PRIO_REALTIME
} osal_priority_t;

typedef struct {
    const char *name;
    uint32_t stack_size;
    osal_priority_t priority;
} osal_thread_attr_t;

/* ====================================== 内核状态/时间 ====================================================== */
bool OSAL_kernel_is_running(void);

osal_tick_t OSAL_tick_get(void);

uint32_t OSAL_tick_freq_hz(void);

uint32_t OSAL_ms_to_ticks(uint32_t ms);

uint32_t OSAL_tick_to_ms(osal_tick_t ticks);

ret_code_t OSAL_delay_ms(uint32_t ms);

bool OSAL_in_isr(void);

bool OSAL_is_timeout(osal_tick_t start_tick, uint32_t duration_ms);

/* ============================================ 临界区 ====================================================== */
/* 旧版保存state 可恢复 + 上下文正确 */
void OSAL_enter_critical(void);

/* 旧版保存state 可恢复 + 上下文正确 */
void OSAL_exit_critical(void);

/* 新版保存state 可恢复 + 上下文正确 */
void OSAL_enter_critical_ex(osal_crit_state_t *state);

/* 新版保存state 可恢复 + 上下文正确 */
void OSAL_exit_critical_ex(osal_crit_state_t state);

void OSAL_enter_critical_from_isr(osal_crit_state_t *state);

void OSAL_exit_critical_from_isr(osal_crit_state_t state);

/* ============================================ 互斥锁 ====================================================== */
ret_code_t OSAL_mutex_create(osal_mutex_t *out, const char *name, bool recursive, bool prio_inherit);

ret_code_t OSAL_mutex_delete(osal_mutex_t mutex);

ret_code_t OSAL_mutex_lock(osal_mutex_t mutex, uint32_t timeout_ms);

ret_code_t OSAL_mutex_unlock(osal_mutex_t mutex);

/* ============================================ 信号量 ====================================================== */
ret_code_t OSAL_sem_create(osal_sem_t *out, const char *name, uint32_t initial_count, uint32_t max_count);

ret_code_t OSAL_sem_delete(osal_sem_t sem);

ret_code_t OSAL_sem_take(osal_sem_t sem, uint32_t timeout_ms);

ret_code_t OSAL_sem_give(osal_sem_t sem);

ret_code_t OSAL_sem_give_from_isr(osal_sem_t sem);

/* ============================================ 消息队列 ====================================================== */
ret_code_t OSAL_msgq_create(osal_msgq_t *out, const char *name, uint32_t item_size, uint32_t item_count);

ret_code_t OSAL_msgq_delete(osal_msgq_t msgq);

ret_code_t OSAL_msgq_put(osal_msgq_t msgq, void *msg, uint32_t timeout_ms);

ret_code_t OSAL_msgq_get(osal_msgq_t msgq, void *msg, uint32_t timeout_ms);

/* ============================================ 线程 && Flags ====================================================== */
ret_code_t OSAL_thread_create(osal_thread_t *out, osal_thread_fn_t fn, void *arg, const osal_thread_attr_t *attr);

osal_thread_t OSAL_thread_self(void);

ret_code_t OSAL_thread_flags_set(osal_thread_t t, osal_flags_t flags);

osal_flags_t OSAL_thread_flags_wait(osal_flags_t flags, osal_flags_wait_t mode, uint32_t timeout_ms);

/* ============================================ Atomic原子操作 ====================================================== */

static inline uint32_t OSAL_atomic_add_u32(volatile uint32_t *v, uint32_t delta) {
    OSAL_enter_critical();
    const uint32_t old = *v;
    *v += delta;
    OSAL_exit_critical();
    return old;
}

static inline bool OSAL_atomic_cas_u32(volatile uint32_t *v, uint32_t expected, uint32_t desired) {
    OSAL_enter_critical();
    const bool ok = (*v == expected);
    if (ok)*v = desired;
    OSAL_exit_critical();
    return ok;
}

#ifdef __cplusplus
}
#endif

#endif //SMARTLOCK_OSAL_H
