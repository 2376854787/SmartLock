#include "stm32_hal_config.h"
/* hal抽象选择宏 */
#if defined(USE_STM32_HAL)
#include "core_mpu.h"

extern uint32_t __flash_start__, __flash_end__;
extern uint32_t __ram_start__, __ram_end__;
extern uint32_t __stack_bottom__, __stack_top__;

bool core_mpu_port_get_map(core_mpu_map_t* out) {
    if (!out) return false;
    out->flash_start  = (uintptr_t)&__flash_start__;
    out->flash_end    = (uintptr_t)&__flash_end__;
    out->ram_start    = (uintptr_t)&__ram_start__;
    out->ram_end      = (uintptr_t)&__ram_end__;
    out->stack_bottom = (uintptr_t)&__stack_bottom__;
    out->stack_top    = (uintptr_t)&__stack_top__;
    return true;
}
#endif
