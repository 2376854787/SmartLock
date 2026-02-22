#include "APP_config.h"

#if (defined(CFG_FEAT_LOG_SYSTEM) && (CFG_FEAT_LOG_SYSTEM == 1)) && \
    (defined(CFG_FEAT_HAL_UART) && (CFG_FEAT_HAL_UART == 1))

#include "Usart1_manage.h"
#include "log_port.h"

/**
 * @brief 应用层覆盖日志端口共享句柄钩子
 * @return 预先初始化的 UART 句柄；无可用句柄时返回 NULL
 */
hal_uart_t* Log_PortAcquireSharedUart(void) {
    return Usart1_GetHalHandle();
}

#endif
