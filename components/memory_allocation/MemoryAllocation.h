//
// 创建：yan，2025/10/18
//

#ifndef TEST_MEMORYALLOCATION_H
#define TEST_MEMORYALLOCATION_H
#include <stdint.h>
#define MEMORY_POND_MAX_SIZE 8192  //单位kb

uint8_t *static_alloc(uint16_t size, uint8_t alignment);

uint16_t query_remain_size(void);

void static_alloc_reset(void);
#endif //TEST_MEMORYALLOCATION_H
