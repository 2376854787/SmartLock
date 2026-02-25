#ifndef SMARTLOCK_MEMORT_POOL1_PORT_H
#define SMARTLOCK_MEMORT_POOL1_PORT_H
#include "compiler_cus.h"
#include "osal.h"
/**
 *
 * @param ctx 上下文 在互斥锁时未锁句柄  /  临界区 无效或者传递信息
 * @param flags 临界区用的flags
 */
CORE_INLINE void mp_lock(void *ctx, uint32_t *flags) {
    (void)flags;
    OSAL_mutex_lock((osal_mutex_t)ctx, 0xFFFFFFFF);
}
/**
 *
 * @param ctx 上下文 在互斥锁时未锁句柄  /  临界区 无效或者传递信息
 * @param flags 临界区 flags
 */
CORE_INLINE void mp_unlock(void *ctx, uint32_t *flags) {
    (void)flags;
    OSAL_mutex_unlock((osal_mutex_t)ctx);
}
#endif  // SMARTLOCK_MEMORT_POOL1_PORT_H
