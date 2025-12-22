#ifndef SMARTLOCK_RB_PORT_H
#define SMARTLOCK_RB_PORT_H

/* FreeRTOS的启用宏 */
#define RB_USE_FREERTOS
#if defined(RB_USE_FREERTOS)
#include "FreeRTOS.h"
#include "task.h"
typedef UBaseType_t rb_isr_state_t;
#define RB_ENTER_CRITICAL()               taskENTER_CRITICAL()
#define RB_EXIT_CRITICAL()                taskEXIT_CRITICAL()
#define RB_ENTER_CRITICAL_FROM_ISR(s)     do{ (s)=taskENTER_CRITICAL_FROM_ISR(); }while(0)
#define RB_EXIT_CRITICAL_FROM_ISR(s)      do{ taskEXIT_CRITICAL_FROM_ISR((s)); }while(0)
#else
#include "cmsis_compiler.h"
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

#define RB_ENTER_CRITICAL()               do{ rb_primask_save(); }while(0)
#define RB_EXIT_CRITICAL()                do{ __enable_irq(); }while(0)
#define RB_ENTER_CRITICAL_FROM_ISR(s)     do{ (s)=rb_primask_save(); }while(0)
#define RB_EXIT_CRITICAL_FROM_ISR(s)      do{ rb_primask_restore((s)); }while(0)
#endif

#endif //SMARTLOCK_RB_PORT_H
