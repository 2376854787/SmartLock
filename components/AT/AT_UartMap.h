//
// Created by yan on 2025/12/15.
//

#ifndef SMARTLOCK_AT_UARTMAP_H
#define SMARTLOCK_AT_UARTMAP_H
#include "stm32f4xx_hal.h"   // UART_HandleTypeDef

typedef struct AT_Manager_t AT_Manager_t; // 前置声明，避免必须包含 AT.h

void AT_BindUart(AT_Manager_t *mgr, UART_HandleTypeDef *huart);

AT_Manager_t *AT_FindMgrByUart(const UART_HandleTypeDef *huart);
#endif //SMARTLOCK_AT_UARTMAP_H
