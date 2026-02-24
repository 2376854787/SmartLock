#include "ramfunc.h"

#include <stdint.h>

#include "assert_cus.h"

extern uint32_t __ramfunc_load_start__;
extern uint32_t __ramfunc_start__;
extern uint32_t __ramfunc_end__;

void ramfunc_init_copy(void) {
    const uintptr_t dst_start = (uintptr_t)&__ramfunc_start__;
    const uintptr_t dst_end   = (uintptr_t)&__ramfunc_end__;
    ASSERT_FATAL(dst_start <= dst_end);
    if (dst_start > dst_end) return;
    uint32_t *src = &__ramfunc_load_start__;
    uint32_t *dst = &__ramfunc_start__;
    while (dst < &__ramfunc_end__) {
        *dst++ = *src++;
    }
    __asm volatile("dsb 0xF" ::: "memory");
    __asm volatile("isb 0xF" ::: "memory");
}
