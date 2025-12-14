//
// Created by yan on 2025/12/10.
//

#ifndef SMARTLOCK_AT_CORE_TASK_H
#define SMARTLOCK_AT_CORE_TASK_H
#include "main.h"
#include "AT.h"
void at_core_task_init(AT_Manager_t *at, UART_HandleTypeDef *uart) ;
void AT_Core_Task(void *argument);
extern AT_Manager_t g_at_manager;
#endif //SMARTLOCK_AT_CORE_TASK_H