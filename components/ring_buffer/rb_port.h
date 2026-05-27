#ifndef SMARTLOCK_RB_PORT_H
#define SMARTLOCK_RB_PORT_H

#include <stdint.h>

/* 说明：
 * - 优先走 OSAL（RTOS/裸机都可以由 OSAL 后端决定如何实现临界区）
 * - 可以显式指定“完全无 OSAL”的纯裸机版本，走下方 else 分支
 *
 * 重要语义约定（两种后端必须一致）：
 * 1. RB_ENTER_CRITICAL(state) / RB_EXIT_CRITICAL(state) 用于“线程态”路径。
 *    必须 save/restore 中断状态，禁止裸调用 __enable_irq()——否则会破坏
 *    嵌套调用者的中断屏蔽状态。state 由调用者声明（rb_isr_state_t 类型）。
 * 2. RB_ENTER_CRITICAL_FROM_ISR(state) / RB_EXIT_CRITICAL_FROM_ISR(state)
 *    用于 ISR 路径，state 由调用者声明并 save/restore。
 * 3. 两种版本都必须包含内存屏障语义，保证临界区内的读写不会越界重排。
 *
 * 历史变更：
 * - 旧版 RB_ENTER_CRITICAL() 无参数，使用 __disable_irq()/__enable_irq() 强制
 *   开关中断，会破坏嵌套调用者的中断状态。新版强制要求传入 state 变量，与
 *   FROM_ISR 版本统一。
 */

#if 1 /* 使用 OSAL 作为统一抽象层 */
#include "osal.h"

typedef osal_crit_state_t rb_isr_state_t;

/* 线程态：使用 ex 版本，自带 save/restore，安全嵌套 */
#define RB_ENTER_CRITICAL(s)         OSAL_enter_critical_ex(&(s))
#define RB_EXIT_CRITICAL(s)          OSAL_exit_critical_ex((s))

/* ISR 态 */
#define RB_ENTER_CRITICAL_FROM_ISR(s) OSAL_enter_critical_from_isr(&(s))
#define RB_EXIT_CRITICAL_FROM_ISR(s)  OSAL_exit_critical_from_isr((s))

#else /* 纯裸机：PRIMASK 方案（不依赖 OSAL） */

#include "cmsis_gcc.h" /* __get_PRIMASK/__set_PRIMASK */
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

/* 线程态：save/restore PRIMASK，避免破坏嵌套调用者的中断状态 */
#define RB_ENTER_CRITICAL(s) \
    do {                     \
        (s) = rb_primask_save(); \
    } while (0)
#define RB_EXIT_CRITICAL(s) \
    do {                    \
        rb_primask_restore((s)); \
    } while (0)

/* ISR 态：与线程态共享实现 */
#define RB_ENTER_CRITICAL_FROM_ISR(s) RB_ENTER_CRITICAL(s)
#define RB_EXIT_CRITICAL_FROM_ISR(s)  RB_EXIT_CRITICAL(s)

#endif

#endif /* SMARTLOCK_RB_PORT_H */
