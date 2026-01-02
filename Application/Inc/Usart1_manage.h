//
// 创建：yan，2025/12/5
//

#ifndef SMARTCLOCK_USART1_TASK_H
#define SMARTCLOCK_USART1_TASK_H
#include "main.h"
#include "RingBuffer.h"
#define DMA_BUFFER_SIZE 512
#define RINGBUFFER_SIZE 2048
bool MyUart_Init(void) ;
void process_dma_data(void);

extern RingBuffer g_rb_uart1;
extern uint8_t DmaBuffer[DMA_BUFFER_SIZE];/* freertos.c HAL_UART_ErrorCallback */
#endif //SMARTCLOCK_USART1_TASK_H
