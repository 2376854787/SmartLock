#ifndef SMARTCLOCK_USART1_TASK_H
#define SMARTCLOCK_USART1_TASK_H

#include "hal_uart.h"

bool MyUart_Init(void);
hal_uart_t* Usart1_GetHalHandle(void);

#endif  // SMARTCLOCK_USART1_TASK_H
