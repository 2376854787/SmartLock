#include "log.h"
#include "usart.h"         /* 获取串口句柄 */
#include "stm32f4xx_hal.h"

/**
 *
 * @param d 数据源
 * @param n 数据大小（）Byte
 * @param user 串口句柄
 * @return
 */
static int Log_uart_send_async(const uint8_t *d, uint16_t n, void *user) {
    UART_HandleTypeDef *huart = (UART_HandleTypeDef *) user;
    return (HAL_UART_Transmit(huart, (uint8_t *) d, n, 100) == HAL_OK) ? 0 : -1;
}

void Log_PortInit(void) {
    Log_SetBackend((log_backend_t){.send_async = Log_uart_send_async, .user = &huart1});
}
