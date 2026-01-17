#include "assert_cus.h"
#include "stm32_hal.h"

/**
 * @brief 覆盖实现 reset
 */
void Assert_PlatformReset(void) {
    NVIC_SystemReset();
}
