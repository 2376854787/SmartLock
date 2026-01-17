#ifndef SMARTLOCK_RB_PORT_H
#define SMARTLOCK_RB_PORT_H

/* 说明：
 * - 优先走 OSAL（RTOS/裸机都可以由 OSAL 后端决定如何实现临界区）
 * - 可以显式指定“完全无 OSAL”的纯裸机版本，走下方 else 分支
 */

#if 1  /* 使用 OSAL 作为统一抽象层 */
#include "osal.h"

typedef osal_crit_state_t rb_isr_state_t;

#define RB_ENTER_CRITICAL()               OSAL_enter_critical()
#define RB_EXIT_CRITICAL()                OSAL_exit_critical()

#define RB_ENTER_CRITICAL_FROM_ISR(s)     OSAL_enter_critical_from_isr(&(s))
#define RB_EXIT_CRITICAL_FROM_ISR(s)      OSAL_exit_critical_from_isr((s))

#else  /* 纯裸机：PRIMASK 方案（不依赖 OSAL） */

#include "cmsis_gcc.h"   /* __get_PRIMASK/__set_PRIMASK */
typedef uint32_t rb_isr_state_t;

static inline uint32_t rb_primask_save(void) {
    uint32_t s = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return s;
}

static inline void rb_primask_restore(uint32_t s) {
    __DMB();
    __set_PRIMASK(s);
}

#define RB_ENTER_CRITICAL_FROM_ISR(s)     do { (s) = rb_primask_save(); } while (0)
#define RB_EXIT_CRITICAL_FROM_ISR(s)      do { rb_primask_restore((s)); } while (0)

/* 线程态临界区：可恢复需更改 保存state。
 */
#define RB_ENTER_CRITICAL()               do { __disable_irq(); __DMB(); } while (0)
#define RB_EXIT_CRITICAL()                do { __DMB(); __enable_irq(); } while (0)

#endif

#endif /* SMARTLOCK_RB_PORT_H */
