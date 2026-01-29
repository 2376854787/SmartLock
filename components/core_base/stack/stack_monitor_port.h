#ifndef SMARTLOCK_STACK_MONITOR_PORT_H
#define SMARTLOCK_STACK_MONITOR_PORT_H

#include <stdint.h>

#include "stack_monitor.h"

/* 按照 .ldl链接脚本实际情况更改 */
/* Linker-provided stack range symbols (see STM32F407XX_FLASH.ld). */
extern uint32_t __stack_bottom__;
extern uint32_t __stack_top__;
/**
 * @brief 返回封装好起始位置的栈结构体
 * @return 栈的起始位置封装的结构体
 * @note  必须按照 .ldl链接脚本实际情况更改
 */
static inline stack_range_t StackMonitor_GetMainStackRange(void) {
    stack_range_t r;
    r.stack_bottom = (uintptr_t)&__stack_bottom__;
    r.stack_top    = (uintptr_t)&__stack_top__;
    return r;
}

/* Backward-compatible alias (historical name). */
static inline stack_range_t StackPaint_GetMainStackRange(void) {
    return StackMonitor_GetMainStackRange();
}

#endif  // SMARTLOCK_STACK_MONITOR_PORT_H
