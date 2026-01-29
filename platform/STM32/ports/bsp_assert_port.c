#include "stm32_hal_config.h"
/* hal抽象选择宏 */
#if defined(USE_STM32_HAL)
#include "assert_cus.h"
#include "log.h"
#include "stm32_hal.h"

/**
 * @brief 覆盖实现 reset
 */
void Assert_PlatformReset(void) {
    NVIC_SystemReset();
}

/**
 * @brief 覆盖实现输出断言失败的环境信息
 * @param msg 消息来源
 */
void Assert_PlatformLog(const char* msg) {
    LOG_E("Assert", msg);
}
#endif
