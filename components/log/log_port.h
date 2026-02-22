#ifndef LOG_PORT_H
#define LOG_PORT_H

#include "hal_uart.h"

/* 应用层可选覆盖：返回共享 UART 句柄；默认弱实现返回 NULL */
hal_uart_t* Log_PortAcquireSharedUart(void);

void Log_PortInit(void);
#endif //LOG_PORT_H
