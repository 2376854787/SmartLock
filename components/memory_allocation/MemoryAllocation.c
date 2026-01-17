#include "APP_config.h"
/* 静态内存分配开启宏 */
#ifdef ENABLE_STATIC_ALLOCATION
#include <stdlib.h>

#include "MemoryAllocation.h"
static uint8_t MemoryPond[MEMORY_POND_MAX_SIZE];
volatile uint16_t MemoryPondIndex = 0;  // 指向还没有被分配的空间的第一个地址

/**
 * @brief 从静态内存池子分配空间返回基准地址
 * @param size 要分配的空间大小
 * @param alignment 要对齐的字节 {1, 2, 4, 8}
 * @retval 返回指针基准地址
 **/
uint8_t *static_alloc(const uint32_t size, const uint8_t alignment) {
    // 1、计算当前指针的地址
    const uintptr_t cur = (uintptr_t)&MemoryPond[MemoryPondIndex];
    uint8_t padding     = 0;
    // 2、计算需要对齐的字节数
    if (alignment > 0 && cur % alignment != 0) {
        padding = alignment - cur % alignment;
    }
    // 3、判断剩余空间是否够
    if (MemoryPondIndex + size + padding > MEMORY_POND_MAX_SIZE) {
        return NULL;
    }
    // 4、进行字节对齐
    MemoryPondIndex += padding;

    // 5、分配内存并返回地址
    uint8_t *ptr = &MemoryPond[MemoryPondIndex];
    MemoryPondIndex += size;
    return ptr;
}

/**
 * @brief 重置静态内存池，将所有已分配空间标记为可用
 * @note  这会使之前所有从该池分配的指针全部失效！
 */
void static_alloc_reset(void) {
    MemoryPondIndex = 0;
}

/**
 *
 * @return 返回当前剩余的空间大小
 */
uint16_t query_remain_size(void) {
    return MEMORY_POND_MAX_SIZE - MemoryPondIndex;
}

#endif