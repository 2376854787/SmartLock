/**
 * @file SchM_Spi_Osal.c
 * @brief SPI SchM 适配层（OSAL 实现）
 */
#include <stddef.h>

#include "SchM_Spi.h"
#include "osal.h"

bool SchM_Spi_KernelIsRunning(void) {
    return OSAL_kernel_is_running();
}

ret_code_t SchM_Spi_LockCreate(SchM_Spi_LockHandleType *out, const char *name, bool recursive,
                               bool prio_inherit) {
    if (out == NULL) return RET_MAKE_PARAM(RET_MOD_OSAL, RET_SUB_OSAL_MUTEX, RET_R_NULL_PTR);
    osal_mutex_t m = NULL;
    const ret_code_t rc = OSAL_mutex_create(&m, name, recursive, prio_inherit);
    if (ret_is_err(rc)) return rc;
    *out = (SchM_Spi_LockHandleType)m;
    return RET_OK;
}

void SchM_Spi_LockDelete(SchM_Spi_LockHandleType lock) {
    if (lock == 0u) return;
    (void)OSAL_mutex_delete((osal_mutex_t)lock);
}

ret_code_t SchM_Spi_Lock(SchM_Spi_LockHandleType lock, uint32_t timeout_ms) {
    if (lock == 0u) return RET_OK;
    return OSAL_mutex_lock((osal_mutex_t)lock, timeout_ms);
}

void SchM_Spi_Unlock(SchM_Spi_LockHandleType lock) {
    if (lock == 0u) return;
    (void)OSAL_mutex_unlock((osal_mutex_t)lock);
}

void SchM_Enter_Spi_ExclusiveArea(SchM_Spi_CritStateType *state) {
    if (state == NULL) return;
    osal_crit_state_t cs = 0u;
    OSAL_enter_critical_ex(&cs);
    *state = (SchM_Spi_CritStateType)cs;
}

void SchM_Exit_Spi_ExclusiveArea(SchM_Spi_CritStateType state) {
    OSAL_exit_critical_ex((osal_crit_state_t)state);
}
