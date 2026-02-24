#include <stddef.h>

#include "assert_cus.h"
#include "log.h"
#include "log_port.h"
#include "ret_code.h"

static volatile uint8_t s_uart_tx_busy = 0u;
static hal_uart_t* s_log_uart          = NULL;

#define LOG_PORT_RET(clas_, err_) \
    RET_MAKE(RET_MOD_LOG, RET_SUB_LOG_CORE, RET_CODE_MAKE((clas_), (err_)))

#ifndef LOG_UART_PORT_ID
#define LOG_UART_PORT_ID HAL_UART_ID_1
#endif

#ifndef LOG_UART_BAUD
#define LOG_UART_BAUD 2000000u
#endif

#ifndef LOG_UART_DATA_BITS
#define LOG_UART_DATA_BITS WORDLENGTH_8B
#endif

#ifndef LOG_UART_STOP_BITS
#define LOG_UART_STOP_BITS STOPBITS_1
#endif

#ifndef LOG_UART_PARITY
#define LOG_UART_PARITY 0u
#endif

#ifndef LOG_UART_FLOW_CTRL
#define LOG_UART_FLOW_CTRL false
#endif

/**
 * @brief 可选钩子：返回一个已初始化的共享 UART 句柄
 * @note 默认返回 NULL，表示由 Log_PortInit 自行打开串口。
 *       应用层可提供同名强实现用于共享现有串口句柄。
 */
__attribute__((weak)) hal_uart_t* Log_PortAcquireSharedUart(void) {
    return NULL;
}

static void Log_UartEvtCb(void* user, const hal_uart_event_t* evt) {
    (void)user;
    if (!evt) return;

    if (evt->type == HAL_UART_EVT_TX_DONE || evt->type == HAL_UART_EVT_ERROR) {
        s_uart_tx_busy = 0u;
        Log_OnTxDoneISR();
    }
}

static int Log_uart_send_async(const uint8_t* d, uint16_t n, void* user) {
    (void)user;
    ASSERT_PARAM((d != NULL) && (n != 0u));
    REQUIRE_RET((d != NULL) && (n != 0u), LOG_PORT_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG));
    if (!s_log_uart) return LOG_PORT_RET(RET_CLASS_STATE, RET_R_NOT_READY);
    if (s_uart_tx_busy) return LOG_PORT_RET(RET_CLASS_STATE, RET_R_BUSY);

    s_uart_tx_busy = 1u;
    const ret_code_t rc = hal_uart_send_async(s_log_uart, d, (uint32_t)n);
    if (ret_is_ok(rc)) return RET_OK;

    s_uart_tx_busy = 0u;
    if (ret_is_busy(rc)) return LOG_PORT_RET(RET_CLASS_STATE, RET_R_BUSY);
    if (ret_is_class(rc, RET_CLASS_PARAM)) return LOG_PORT_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    if (ret_is_class(rc, RET_CLASS_STATE)) return LOG_PORT_RET(RET_CLASS_STATE, RET_R_NOT_READY);
    return LOG_PORT_RET(RET_CLASS_FATAL, RET_R_PANIC);
}

void Log_PortInit(void) {
    if (!s_log_uart) s_log_uart = Log_PortAcquireSharedUart();

    if (!s_log_uart) {
        const hal_uart_cfg_t cfg = {
            .baud         = LOG_UART_BAUD,
            .data_bits    = LOG_UART_DATA_BITS,
            .stop_bits    = LOG_UART_STOP_BITS,
            .parity       = (uint8_t)LOG_UART_PARITY,
            .flow_ctrl    = LOG_UART_FLOW_CTRL,
            .isCompatible = true,
        };
        if (ret_is_ok(hal_uart_open(LOG_UART_PORT_ID, &cfg, &s_log_uart))) {
            (void)hal_uart_rx_start(s_log_uart);
        }
    }

    if (s_log_uart) {
        (void)hal_uart_set_evt_cb(s_log_uart, Log_UartEvtCb, NULL);
    }

    Log_SetBackend((log_backend_t){.send_async = Log_uart_send_async, .user = NULL});
}
