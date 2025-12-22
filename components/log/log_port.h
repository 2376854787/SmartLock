#ifndef SMARTLOCK_LOG_PORT_H
#define SMARTLOCK_LOG_PORT_H

void Log_PortInit(void);
void LOG_UART_TxCpltCallback(UART_HandleTypeDef *huart);
#endif //SMARTLOCK_LOG_PORT_H