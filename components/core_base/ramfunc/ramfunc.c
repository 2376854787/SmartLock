#include "ramfunc.h"

#include <stdint.h>

extern uint32_t __ramfunc_load_start__;
extern uint32_t __ramfunc_start__;
extern uint32_t __ramfunc_end__;

void ramfunc_init_copy(void) {
    uint32_t *src = &__ramfunc_load_start__;
    uint32_t *dst = &__ramfunc_start__;
    while (dst < &__ramfunc_end__) {
        *dst++ = *src++;
    }
    __asm volatile("dsb 0xF" ::: "memory");
    __asm volatile("isb 0xF" ::: "memory");
}
