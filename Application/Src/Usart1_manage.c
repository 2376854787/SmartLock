#include "Usart1_manage.h"

#include <stdio.h>

#include "hal_uart.h"
#include "ret_code_t.h"
static hal_uart_t* s_uart1_hal = NULL;

#ifndef USART1_HAL_PORT_ID
#define USART1_HAL_PORT_ID HAL_UART_ID_1
#endif

#ifndef USART1_BAUD
#define USART1_BAUD 2000000u
#endif

#ifndef USART1_DATA_BITS
#define USART1_DATA_BITS HAL_UART_DATA_BITS_8
#endif

#ifndef USART1_STOP_BITS
#define USART1_STOP_BITS HAL_UART_STOP_BITS_1
#endif

#ifndef USART1_PARITY
#define USART1_PARITY 0u
#endif

#ifndef USART1_FLOW_CTRL
#define USART1_FLOW_CTRL false
#endif

bool MyUart_Init(void) {
    if (s_uart1_hal == NULL) {
        const hal_uart_cfg_t cfg = {
            .baud      = USART1_BAUD,
            .data_bits = USART1_DATA_BITS,
            .stop_bits = USART1_STOP_BITS,
            .parity    = (uint8_t)USART1_PARITY,
            .flow_ctrl = USART1_FLOW_CTRL,
        };

        if (ret_is_err(hal_uart_init(USART1_HAL_PORT_ID, &cfg, &s_uart1_hal))) {
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
