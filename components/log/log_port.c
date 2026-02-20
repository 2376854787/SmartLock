#include <stdio.h>

#include "hal_uart.h"
#include "log.h"
#include "ret_code.h"
#include "Usart1_manage.h"
#include "usart.h"

static volatile uint8_t s_uart_tx_busy = 0;
static hal_uart_t* s_log_uart          = NULL;

#define LOG_PORT_RET(clas_, err_) \
    RET_MAKE(RET_MOD_LOG, RET_SUB_LOG_CORE, RET_CODE_MAKE((clas_), (err_)))

static int Log_uart_send_async(const uint8_t* d, uint16_t n, void* user) {
    (void)user;
    if (!d || n == 0u) return LOG_PORT_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
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
    if (!s_log_uart) s_log_uart = Usart1_GetHalHandle();

    if (!s_log_uart) {
        hal_uart_cfg_t cfg;
        cfg.baud         = huart1.Init.BaudRate;
        cfg.data_bits    = (huart1.Init.WordLength == UART_WORDLENGTH_9B) ? WORDLENGTH_9B : WORDLENGTH_8B;
        cfg.stop_bits    = (huart1.Init.StopBits == UART_STOPBITS_2) ? STOPBITS_2 : STOPBITS_1;
        cfg.parity       = (uint8_t)huart1.Init.Parity;
        cfg.flow_ctrl    = (huart1.Init.HwFlowCtl != UART_HWCONTROL_NONE);
        cfg.isCompatible = true;
        if (ret_is_ok(hal_uart_open(HAL_UART_ID_0, &cfg, &s_log_uart))) {
            (void)hal_uart_rx_start(s_log_uart);
        }
    }

    Log_SetBackend((log_backend_t){.send_async = Log_uart_send_async, .user = NULL});
}

void LOG_UART_TxCpltCallback(UART_HandleTypeDef* huart) {
    if (huart == &huart1) {
        s_uart_tx_busy = 0u;
        Log_OnTxDoneISR();/* 通知 LogTask：这笔发送结束 */
    }
}
