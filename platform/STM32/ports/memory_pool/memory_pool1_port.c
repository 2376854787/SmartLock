#include "utils_def.h"
#include "memory_pool1.h"
#include "stm32_hal.h"
/**
 *
 * @param addr 基地址
 * @param length
 */
void my_cache_clean(void *addr, uint32_t length) {
#if (defined(CFG_PARAM_STM32_DMA_CACHE_CLEAN) && (CFG_PARAM_STM32_DMA_CACHE_CLEAN == 1))
    if (!addr || length == 0u) return;
    const uintptr_t start = (uintptr_t)ALIGN_DOWN((uintptr_t)addr, 32u);
    const uintptr_t end   = (uintptr_t)ALIGN_UP((uintptr_t)addr + (uintptr_t)length, 32u);
    SCB_CleanDCache_by_Addr((uint32_t*)start, (int32_t)(end - start));
#else
    (void)addr;
    (void)length;
#endif
}

/* 适配 Invalidate (失效) */
void my_cache_invalidate(void *addr, uint32_t length) {
#if (defined(CFG_PARAM_STM32_DMA_CACHE_INVALIDATE) && (CFG_PARAM_STM32_DMA_CACHE_INVALIDATE == 1))
    if (!addr || length == 0u) return;
    const uintptr_t start = (uintptr_t)ALIGN_DOWN((uintptr_t)addr, 32u);
    const uintptr_t end   = (uintptr_t)ALIGN_UP((uintptr_t)addr + (uintptr_t)length, 32u);
    SCB_InvalidateDCache_by_Addr((uint32_t*)start, (int32_t)(end - start));
#else
    (void)addr;
    (void)length;
#endif
}


