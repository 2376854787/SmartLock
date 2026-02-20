#include "utils_def.h"
#include "memory_pool1.h"
#include "stm32_hal.h"
/**
 *
 * @param addr 基地址
 * @param length
 */
void my_cache_clean(void *addr, uint32_t length) {
#if defined(SCB_CleanDCache_by_Addr)
    if (!ptr || len == 0u) return;
    const uintptr_t start = (uintptr_t)ALIGN_DOWN((uintptr_t)ptr, 32u);
    const uintptr_t end   = (uintptr_t)ALIGN_UP((uintptr_t)ptr + (uintptr_t)len, 32u);
    SCB_CleanDCache_by_Addr((uint32_t*)start, (int32_t)(end - start));
#else
    (void)addr;
    (void)length;
#endif
}

/* 适配 Invalidate (失效) */
void my_cache_invalidate(void *addr, uint32_t length) {
#if defined(SCB_InvalidateDCache_by_Addr)
    if (!ptr || len == 0u) return;
    const uintptr_t start = (uintptr_t)ALIGN_DOWN((uintptr_t)ptr, 32u);
    const uintptr_t end   = (uintptr_t)ALIGN_UP((uintptr_t)ptr + (uintptr_t)len, 32u);
    SCB_InvalidateDCache_by_Addr((uint32_t*)start, (int32_t)(end - start));
#else
    (void)addr;
    (void)length;
#endif
}
