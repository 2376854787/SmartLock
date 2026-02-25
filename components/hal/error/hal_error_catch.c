#include "APP_config.h"
#include "hal_error_catch.h"

#if defined(CFG_FEAT_HAL_ERROR_CATCH) && (CFG_FEAT_HAL_ERROR_CATCH == 1)

#include <stddef.h>

#include "assert_cus.h"
#include "hal_gpio.h"
#include "hal_spi.h"
#include "hal_uart.h"
#include "hal_wdg.h"
#include "osal.h"
#if defined(CFG_FEAT_LOG_SYSTEM) && (CFG_FEAT_LOG_SYSTEM == 1)
#include "log.h"
#endif

void hal_spi_on_port_error(ret_code_t rc_port, ret_code_t rc_hal, const char* api, uint32_t arg0,
                           uint32_t arg1) {
    ASSERT_PARAM(api != NULL);
#if defined(CFG_FEAT_LOG_SYSTEM) && (CFG_FEAT_LOG_SYSTEM == 1) && \
    defined(CFG_PARAM_SPI_LOG_PORT_ERR) && (CFG_PARAM_SPI_LOG_PORT_ERR == 1)
#if defined(CFG_PARAM_SPI_LOG_PORT_ERR_IN_ISR) && (CFG_PARAM_SPI_LOG_PORT_ERR_IN_ISR == 1)
    if (ret_is_err(rc_port)) {
#else
    if (ret_is_err(rc_port) && !OSAL_in_isr()) {
#endif
        LOG_E("HAL_SPI", "api:%s port:0x%08lX->hal:0x%08lX arg0:%lu arg1:%lu",
              (api != NULL) ? api : "unknown", (unsigned long)rc_port, (unsigned long)rc_hal,
              (unsigned long)arg0, (unsigned long)arg1);
    }
#else
    (void)rc_port;
    (void)rc_hal;
    (void)api;
    (void)arg0;
    (void)arg1;
#endif
}

void hal_uart_on_port_error(ret_code_t rc_port, ret_code_t rc_hal, const char* api, uint32_t arg0,
                            uint32_t arg1) {
    ASSERT_PARAM(api != NULL);
#if defined(CFG_FEAT_LOG_SYSTEM) && (CFG_FEAT_LOG_SYSTEM == 1) && \
    defined(CFG_PARAM_UART_LOG_PORT_ERR) && (CFG_PARAM_UART_LOG_PORT_ERR == 1)
#if defined(CFG_PARAM_UART_LOG_PORT_ERR_IN_ISR) && (CFG_PARAM_UART_LOG_PORT_ERR_IN_ISR == 1)
    if (ret_is_err(rc_port)) {
#else
    if (ret_is_err(rc_port) && !OSAL_in_isr()) {
#endif
        LOG_E("HAL_UART", "api:%s port:0x%08lX->hal:0x%08lX arg0:%lu arg1:%lu",
              (api != NULL) ? api : "unknown", (unsigned long)rc_port, (unsigned long)rc_hal,
              (unsigned long)arg0, (unsigned long)arg1);
    }
#else
    (void)rc_port;
    (void)rc_hal;
    (void)api;
    (void)arg0;
    (void)arg1;
#endif
}

void hal_gpio_on_port_error(ret_code_t rc_port, ret_code_t rc_hal, const char* api, uint32_t arg0,
                            uint32_t arg1) {
    ASSERT_PARAM(api != NULL);
#if defined(CFG_FEAT_LOG_SYSTEM) && (CFG_FEAT_LOG_SYSTEM == 1) && \
    defined(CFG_PARAM_GPIO_LOG_PORT_ERR) && (CFG_PARAM_GPIO_LOG_PORT_ERR == 1)
#if defined(CFG_PARAM_GPIO_LOG_PORT_ERR_IN_ISR) && (CFG_PARAM_GPIO_LOG_PORT_ERR_IN_ISR == 1)
    if (ret_is_err(rc_port)) {
#else
    if (ret_is_err(rc_port) && !OSAL_in_isr()) {
#endif
        LOG_E("HAL_GPIO", "api:%s port:0x%08lX->hal:0x%08lX arg0:%lu arg1:%lu",
              (api != NULL) ? api : "unknown", (unsigned long)rc_port, (unsigned long)rc_hal,
              (unsigned long)arg0, (unsigned long)arg1);
    }
#else
    (void)rc_port;
    (void)rc_hal;
    (void)api;
    (void)arg0;
    (void)arg1;
#endif
}

void hal_wdg_on_port_error(ret_code_t rc_port, ret_code_t rc_hal, const char* api, uint32_t arg0,
                           uint32_t arg1) {
    ASSERT_PARAM(api != NULL);
#if defined(CFG_FEAT_LOG_SYSTEM) && (CFG_FEAT_LOG_SYSTEM == 1) && \
    defined(CFG_PARAM_WDG_LOG_PORT_ERR) && (CFG_PARAM_WDG_LOG_PORT_ERR == 1)
#if defined(CFG_PARAM_WDG_LOG_PORT_ERR_IN_ISR) && (CFG_PARAM_WDG_LOG_PORT_ERR_IN_ISR == 1)
    if (ret_is_err(rc_port)) {
#else
    if (ret_is_err(rc_port) && !OSAL_in_isr()) {
#endif
        LOG_E("HAL_WDG", "api:%s port:0x%08lX->hal:0x%08lX arg0:%lu arg1:%lu",
              (api != NULL) ? api : "unknown", (unsigned long)rc_port, (unsigned long)rc_hal,
              (unsigned long)arg0, (unsigned long)arg1);
    }
#else
    (void)rc_port;
    (void)rc_hal;
    (void)api;
    (void)arg0;
    (void)arg1;
#endif
}

#endif
