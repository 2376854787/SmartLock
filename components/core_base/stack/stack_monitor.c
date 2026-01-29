#include "stack_monitor.h"

#include "stack_monitor_port.h"

static inline uintptr_t read_sp(void) {
#if defined(__GNUC__) || defined(__clang__)
    uintptr_t sp;
    __asm__ __volatile__("mov %0, sp" : "=r"(sp));
    return sp;
#else
    return 0;
#endif
}

void StackMonitor_Init(const stack_range_t* stack_range) {
    if (!stack_range || stack_range->stack_bottom >= stack_range->stack_top) return;

    const uintptr_t sp       = read_sp();
    const uintptr_t safe_top = (sp > 64u) ? (sp - 64u) : sp;

    uintptr_t start = stack_range->stack_bottom;
    uintptr_t end   = (safe_top < stack_range->stack_top) ? safe_top : stack_range->stack_top;

    start = (start + 3u) & ~((uintptr_t)3u);
    end   = end & ~((uintptr_t)3u);
    if (end <= start) return;

    for (uintptr_t i = start; i < end; i += 4u) {
        *(volatile uint32_t*)i = STACK_MONITOR_MAGIC;
    }
}

size_t StackMonitor_GetMinFreeSize(const stack_range_t* r) {
    if (!r || r->stack_bottom >= r->stack_top) return 0;

    const uintptr_t start = (r->stack_bottom + 3u) & ~((uintptr_t)3u);
    const uintptr_t end   = (r->stack_top) & ~((uintptr_t)3u);

    uintptr_t p = start;
    for (; p < end; p += 4u) {
        if (*(volatile uint32_t*)p != STACK_MONITOR_MAGIC) break;
    }

    return (size_t)(p - start);
}

uint32_t StackMonitor_GetMinFreePerMill(const stack_range_t* r) {
    const size_t free_size  = StackMonitor_GetMinFreeSize(r);
    const size_t total_size =
        (r && r->stack_top > r->stack_bottom) ? (r->stack_top - r->stack_bottom) : 0u;

    if (total_size == 0u) return 0u;
    return (uint32_t)((free_size * 1000u) / total_size);
}
