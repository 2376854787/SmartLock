#include "Usart1_manage.h"

#include <stdio.h>

#include "hal_uart.h"
#include "ret_code.h"
#include "usart.h"

static hal_uart_t* s_uart1_hal = NULL;

bool MyUart_Init(void) {
    if (s_uart1_hal == NULL) {
        hal_uart_cfg_t cfg;
        cfg.baud         = huart1.Init.BaudRate;
        cfg.data_bits    = (huart1.Init.WordLength == UART_WORDLENGTH_9B) ? WORDLENGTH_9B : WORDLENGTH_8B;
        cfg.stop_bits    = (huart1.Init.StopBits == UART_STOPBITS_2) ? STOPBITS_2 : STOPBITS_1;
        cfg.parity       = (uint8_t)huart1.Init.Parity;
        cfg.flow_ctrl    = (huart1.Init.HwFlowCtl != UART_HWCONTROL_NONE);
        cfg.isCompatible = true;

        if (ret_is_err(hal_uart_open(HAL_UART_ID_0, &cfg, &s_uart1_hal))) {
            printf("UART HAL init failed\n");
            return false;
        }

        if (ret_is_err(hal_uart_rx_start(s_uart1_hal))) {
            printf("UART HAL rx start failed\n");
            return false;
        }
    }
    return true;
}

hal_uart_t* Usart1_GetHalHandle(void) {
    return s_uart1_hal;
}

