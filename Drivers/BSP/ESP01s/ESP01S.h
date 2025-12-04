#ifndef INC_ESP01S_H_
#define INC_ESP01S_H_
#include <stdbool.h>
#include <stdint.h>

#include "cmsis_os2.h"
#include "RingBuffer.h"
#include  "main.h"
typedef struct {
    UART_HandleTypeDef *uart_p; /* 需要使用的串口 */
    RingBuffer rb; /* 环形缓冲区的句柄 */
    uint16_t rb_rx_size; /* 环形缓冲区初始化大小（字节）*/
    uint8_t rx_buffer[512]; /* 临时缓冲区 */
    uint16_t rx_len; /* 临时缓冲区接收的数据长度 */
}ESP01S_Handle;
extern osMutexId_t esp01s_Mutex01Handle;
extern ESP01S_Handle g_esp01_handle;
extern volatile uint8_t g_esp01s_flag;
bool command_send(UART_HandleTypeDef* huart, const uint8_t *command, char *wait_rsu, uint16_t max_wait_time) ;
void esp01s_Init(UART_HandleTypeDef *huart, uint16_t rb_size);
#endif /* INC_ESP01S_H_ */
