//
// Created by yan on 2025/12/5.
//

#include "Usart1_manage.h"

#include <stdio.h>

#include "log.h"
#include "MemoryAllocation.h"
#include "usart.h"
#include "ret_code.h"
extern DMA_HandleTypeDef hdma_usart1_rx;
extern DMA_HandleTypeDef hdma_usart1_tx;
RingBuffer g_rb_uart1 = {
    .name = "USART1 RingBuffer"
};

uint8_t DmaBuffer[DMA_BUFFER_SIZE];

bool MyUart_Init(void) {
    if (ret_is_err(CreateRingBuffer(&g_rb_uart1,RINGBUFFER_SIZE))) {
        printf("环形缓冲区初始化失败");
        return false;
    }
    LOG_W("heap", "%uKB- %u空间还剩余 %u", MEMORY_POND_MAX_SIZE, RINGBUFFER_SIZE, query_remain_size());
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, DmaBuffer, DMA_BUFFER_SIZE);
    printf("环形缓冲区初始化成功 %p\n", &g_rb_uart1);
    return true;
}

void process_dma_data(void) {
    static uint16_t last_pos = 0;
    //1、获取当前的位置
    uint32_t curpos = DMA_BUFFER_SIZE - __HAL_DMA_GET_COUNTER(&hdma_usart1_rx);

    uint32_t write_size = curpos - last_pos; //没有回返
    uint32_t write_size2 = DMA_BUFFER_SIZE - last_pos; //数据回返需要写入得后面部分得长度
    //2、判断数据是否更新
    if (last_pos == curpos) return;


    //3、搬运数据到环形缓冲区  //当前数据没有回返
    if (curpos > last_pos) {
        if (ret_is_err(WriteRingBuffer(&g_rb_uart1, &DmaBuffer[last_pos], &write_size, 0)))
            printf("写入失败\n");
    } else {
        //数据回返
        if (ret_is_err(WriteRingBuffer(&g_rb_uart1, &DmaBuffer[last_pos], &write_size2, 0))) {
            printf("Write Error of RingBuffer\n");
        }

        WriteRingBuffer(&g_rb_uart1, &DmaBuffer[0], &curpos, 0);
    }


    //4、更新位置
    last_pos = curpos;
}
