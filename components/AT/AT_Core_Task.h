//
// Created by yan on 2025/12/10.
//

#ifndef SMARTLOCK_AT_CORE_TASK_H
#define SMARTLOCK_AT_CORE_TASK_H
#include "AT.h"
#include "AT_UartMap.h"
void AT_Manage_TxCpltCallback(UART_HandleTypeDef *huart);
void at_core_task_init(AT_Manager_t *at, UART_HandleTypeDef *uart);
extern AT_Manager_t g_at_manager;
#endif  // SMARTLOCK_AT_CORE_TASK_H