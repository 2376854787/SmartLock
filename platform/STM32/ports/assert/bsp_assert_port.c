#include "APP_config.h"
/* hal抽象选择宏 */
#if (defined(CFG_TARGET_PLATFORM_STM32_HAL) && (CFG_TARGET_PLATFORM_STM32_HAL == 1))
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
    //会造成递归等待之后修复调用RAW  LOG_E("Assert", msg);
}
#endif


