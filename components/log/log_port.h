#ifndef LOG_PORT_H
#define LOG_PORT_H

void Log_PortInit(void);
void LOG_UART_TxCpltCallback(UART_HandleTypeDef *huart);
#endif //LOG_PORT_H