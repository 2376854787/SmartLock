#include "stm32_hal_config.h"
/* hal抽象选择宏 */
#if defined(USE_STM32_HAL)
#include "assert_cus.h"
#include "stm32_hal.h"

/**
 * @brief 覆盖实现 reset
 */
void Assert_PlatformReset(void) {
    NVIC_SystemReset();
}

#endif
