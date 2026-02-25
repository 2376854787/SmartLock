#ifndef SMARTLOCK_APP_CONFIG_H
#define SMARTLOCK_APP_CONFIG_H

/* ============================================================================
 *  配置总入口
 * 加载顺序：L1 目标层 -> L2 产品层 -> L3 平台层
 * ============================================================================
 */

/* L1: 目标层（平台/系列） */
#include "target/target_config.h"
/* L2: 产品层（模块开关/策略） */
#include "product/product.h"
/* L3: 平台层（平台端口参数） */
#include "stm32_hal_config.h"
/* 实际的 hal 按需替换*/
#include "stm32_hal.h"

/* ------------------- 编译期一致性检查 ------------------- */

/* 功能开关必须是 0/1 */
#if ((CFG_FEAT_LOG_SYSTEM != 0) && (CFG_FEAT_LOG_SYSTEM != 1))
#error "CFG_FEAT_LOG_SYSTEM 必须为 0 或 1。"
#endif
#if ((CFG_FEAT_AT_SYSTEM != 0) && (CFG_FEAT_AT_SYSTEM != 1))
#error "CFG_FEAT_AT_SYSTEM 必须为 0 或 1。"
#endif
#if ((CFG_FEAT_ASSERT_SYSTEM != 0) && (CFG_FEAT_ASSERT_SYSTEM != 1))
#error "CFG_FEAT_ASSERT_SYSTEM 必须为 0 或 1。"
#endif
#if ((CFG_FEAT_STATIC_ALLOCATION != 0) && (CFG_FEAT_STATIC_ALLOCATION != 1))
#error "CFG_FEAT_STATIC_ALLOCATION 必须为 0 或 1。"
#endif
#if ((CFG_FEAT_RINGBUFFER_SYSTEM != 0) && (CFG_FEAT_RINGBUFFER_SYSTEM != 1))
#error "CFG_FEAT_RINGBUFFER_SYSTEM 必须为 0 或 1。"
#endif
#if ((CFG_FEAT_HFSM_SYSTEM != 0) && (CFG_FEAT_HFSM_SYSTEM != 1))
#error "CFG_FEAT_HFSM_SYSTEM 必须为 0 或 1。"
#endif
#if ((CFG_FEAT_KEYS != 0) && (CFG_FEAT_KEYS != 1))
#error "CFG_FEAT_KEYS 必须为 0 或 1。"
#endif
#if ((CFG_FEAT_CRC16 != 0) && (CFG_FEAT_CRC16 != 1))
#error "CFG_FEAT_CRC16 必须为 0 或 1。"
#endif
#if ((CFG_FEAT_MEMORY_POOL != 0) && (CFG_FEAT_MEMORY_POOL != 1))
#error "CFG_FEAT_MEMORY_POOL 必须为 0 或 1。"
#endif
#if ((CFG_FEAT_HAL_WDG != 0) && (CFG_FEAT_HAL_WDG != 1))
#error "CFG_FEAT_HAL_WDG 必须为 0 或 1。"
#endif
#if ((CFG_FEAT_WDG_SUPERVISOR != 0) && (CFG_FEAT_WDG_SUPERVISOR != 1))
#error "CFG_FEAT_WDG_SUPERVISOR 必须为 0 或 1。"
#endif
#if ((CFG_FEAT_HAL_GPIO != 0) && (CFG_FEAT_HAL_GPIO != 1))
#error "CFG_FEAT_HAL_GPIO 必须为 0 或 1。"
#endif
#if ((CFG_FEAT_HAL_UART != 0) && (CFG_FEAT_HAL_UART != 1))
#error "CFG_FEAT_HAL_UART 必须为 0 或 1。"
#endif
#if ((CFG_FEAT_HAL_SPI != 0) && (CFG_FEAT_HAL_SPI != 1))
#error "CFG_FEAT_HAL_SPI 必须为 0 或 1。"
#endif
#if ((CFG_FEAT_HAL_I2C != 0) && (CFG_FEAT_HAL_I2C != 1))
#error "CFG_FEAT_HAL_I2C 必须为 0 或 1。"
#endif
#if ((CFG_FEAT_HAL_ERROR_CATCH != 0) && (CFG_FEAT_HAL_ERROR_CATCH != 1))
#error "CFG_FEAT_HAL_ERROR_CATCH 必须为 0 或 1。"
#endif
#if ((CFG_FEAT_HAL_TIME != 0) && (CFG_FEAT_HAL_TIME != 1))
#error "CFG_FEAT_HAL_TIME 必须为 0 或 1。"
#endif
#if ((CFG_FEAT_SOFT_I2C != 0) && (CFG_FEAT_SOFT_I2C != 1))
#error "CFG_FEAT_SOFT_I2C 必须为 0 或 1。"
#endif
#if ((CFG_FEAT_GT911 != 0) && (CFG_FEAT_GT911 != 1))
#error "CFG_FEAT_GT911 必须为 0 或 1。"
#endif
#if ((CFG_FEAT_OSAL_BACKEND_CMSIS_OS2 != 0) && (CFG_FEAT_OSAL_BACKEND_CMSIS_OS2 != 1))
#error "CFG_FEAT_OSAL_BACKEND_CMSIS_OS2 必须为 0 或 1。"
#endif
#if ((CFG_FEAT_OSAL_CRITICAL_FREERTOS != 0) && (CFG_FEAT_OSAL_CRITICAL_FREERTOS != 1))
#error "CFG_FEAT_OSAL_CRITICAL_FREERTOS 必须为 0 或 1。"
#endif
#if ((CFG_PARAM_SPI_LOG_PORT_ERR != 0) && (CFG_PARAM_SPI_LOG_PORT_ERR != 1))
#error "CFG_PARAM_SPI_LOG_PORT_ERR 必须为 0 或 1。"
#endif
#if ((CFG_PARAM_SPI_LOG_PORT_ERR_IN_ISR != 0) && (CFG_PARAM_SPI_LOG_PORT_ERR_IN_ISR != 1))
#error "CFG_PARAM_SPI_LOG_PORT_ERR_IN_ISR 必须为 0 或 1。"
#endif
#if ((CFG_PARAM_UART_LOG_PORT_ERR != 0) && (CFG_PARAM_UART_LOG_PORT_ERR != 1))
#error "CFG_PARAM_UART_LOG_PORT_ERR 必须为 0 或 1。"
#endif
#if ((CFG_PARAM_UART_LOG_PORT_ERR_IN_ISR != 0) && (CFG_PARAM_UART_LOG_PORT_ERR_IN_ISR != 1))
#error "CFG_PARAM_UART_LOG_PORT_ERR_IN_ISR 必须为 0 或 1。"
#endif
#if ((CFG_PARAM_GPIO_LOG_PORT_ERR != 0) && (CFG_PARAM_GPIO_LOG_PORT_ERR != 1))
#error "CFG_PARAM_GPIO_LOG_PORT_ERR 必须为 0 或 1。"
#endif
#if ((CFG_PARAM_GPIO_LOG_PORT_ERR_IN_ISR != 0) && (CFG_PARAM_GPIO_LOG_PORT_ERR_IN_ISR != 1))
#error "CFG_PARAM_GPIO_LOG_PORT_ERR_IN_ISR 必须为 0 或 1。"
#endif
#if ((CFG_PARAM_WDG_LOG_PORT_ERR != 0) && (CFG_PARAM_WDG_LOG_PORT_ERR != 1))
#error "CFG_PARAM_WDG_LOG_PORT_ERR 必须为 0 或 1。"
#endif
#if ((CFG_PARAM_WDG_LOG_PORT_ERR_IN_ISR != 0) && (CFG_PARAM_WDG_LOG_PORT_ERR_IN_ISR != 1))
#error "CFG_PARAM_WDG_LOG_PORT_ERR_IN_ISR 必须为 0 或 1。"
#endif

/* 依赖关系检查 */
#if (defined(CFG_FEAT_SOFT_I2C) && (CFG_FEAT_SOFT_I2C == 1)) && \
    (!defined(CFG_FEAT_HAL_GPIO) || (CFG_FEAT_HAL_GPIO != 1))
#error "启用 CFG_FEAT_SOFT_I2C 时，CFG_FEAT_HAL_GPIO 必须为 1。"
#endif

#if (defined(CFG_FEAT_GT911) && (CFG_FEAT_GT911 == 1)) && \
    (!defined(CFG_FEAT_SOFT_I2C) || (CFG_FEAT_SOFT_I2C != 1))
#error "启用 CFG_FEAT_GT911 时，CFG_FEAT_SOFT_I2C 必须为 1。"
#endif

#if (defined(CFG_FEAT_WDG_SUPERVISOR) && (CFG_FEAT_WDG_SUPERVISOR == 1)) && \
    (!defined(CFG_FEAT_HAL_WDG) || (CFG_FEAT_HAL_WDG != 1))
#error "启用 CFG_FEAT_WDG_SUPERVISOR 时，CFG_FEAT_HAL_WDG 必须为 1。"
#endif

#endif /* SMARTLOCK_APP_CONFIG_H */

