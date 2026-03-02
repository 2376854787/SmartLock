#ifndef TEST_OSAL_H
#define TEST_OSAL_H

#include <stdbool.h>
#include <stdint.h>

#include "ret_code.h"

typedef uintptr_t osal_crit_state_t;
typedef uintptr_t osal_mutex_t;
typedef uintptr_t osal_sem_t;
typedef uintptr_t osal_msgq_t;
typedef uintptr_t osal_thread_t;
typedef uint32_t osal_flags_t;
typedef uint32_t osal_priority_t;

typedef struct {
    const char *name;
    uint32_t stack_size;
    osal_priority_t priority;
} osal_thread_attr_t;

#ifndef OSAL_WAIT_FOREVER
#define OSAL_WAIT_FOREVER 0xFFFFFFFFu
#endif

#ifndef OSAL_FLAGS_WAIT_ANY
#define OSAL_FLAGS_WAIT_ANY 0u
#endif

#ifndef OSAL_PRIO_NORMAL
#define OSAL_PRIO_NORMAL 0u
#endif

void OSAL_enter_critical_ex(osal_crit_state_t *state);
void OSAL_exit_critical_ex(osal_crit_state_t state);
void OSAL_enter_critical_from_isr(osal_crit_state_t *state);
void OSAL_exit_critical_from_isr(osal_crit_state_t state);
bool OSAL_kernel_is_running(void);
ret_code_t OSAL_mutex_create(osal_mutex_t *out, const char *name, bool recursive, bool prio_inherit);
ret_code_t OSAL_mutex_delete(osal_mutex_t mutex);
ret_code_t OSAL_mutex_lock(osal_mutex_t mutex, uint32_t timeout_ms);
ret_code_t OSAL_mutex_unlock(osal_mutex_t mutex);
ret_code_t OSAL_sem_create(osal_sem_t *out, const char *name, uint32_t initial_count,
                           uint32_t max_count);
ret_code_t OSAL_sem_delete(osal_sem_t sem);
ret_code_t OSAL_sem_take(osal_sem_t sem, uint32_t timeout_ms);
ret_code_t OSAL_sem_give(osal_sem_t sem);
ret_code_t OSAL_sem_give_from_isr(osal_sem_t sem);
ret_code_t OSAL_delay_ms(uint32_t delay_ms);
ret_code_t OSAL_msgq_put(osal_msgq_t queue, void *item, uint32_t timeout_ms);
ret_code_t OSAL_thread_flags_set(osal_thread_t thread, osal_flags_t flags);
ret_code_t OSAL_thread_create(osal_thread_t *out, void (*entry)(void *), void *arg,
                              const osal_thread_attr_t *attr);
osal_flags_t OSAL_thread_flags_wait(osal_flags_t flags, uint32_t options, uint32_t timeout_ms);

static inline bool OSAL_in_isr(void) {
    return false;
}

#endif /* TEST_OSAL_H */
