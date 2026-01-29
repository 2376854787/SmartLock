#ifndef SMARTLOCK_STACK_MONITOR_H
#define SMARTLOCK_STACK_MONITOR_H

#include <stddef.h>
#include <stdint.h>

#ifndef STACK_MONITOR_MAGIC
#define STACK_MONITOR_MAGIC 0xdeadbeefu
#endif

typedef struct {
    uintptr_t stack_bottom;
    uintptr_t stack_top;
} stack_range_t;

void StackMonitor_Init(const stack_range_t* stack_range);
size_t StackMonitor_GetMinFreeSize(const stack_range_t* r);
uint32_t StackMonitor_GetMinFreePerMill(const stack_range_t* r);

#endif  // SMARTLOCK_STACK_MONITOR_H
