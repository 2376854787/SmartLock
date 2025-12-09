// 文件名: dma_logger.c

#include "MyPrintf.h"
#include "RingBuffer.h"       // 你的环形缓冲区实现
#include "MemoryAllocation.h" // 你的静态内存分配实现
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "log.h"
#include "task.h"

/* --------------------------- 模块配置和私有变量 --------------------------- */

#define LOG_TEMP_BUFFER_SIZE 256
#define LOG_RING_BUFFER_SIZE 1024

static UART_HandleTypeDef *g_log_uart = NULL;
static RingBuffer g_log_ring_buffer;
static volatile bool g_dma_tx_busy = false;
static uint8_t g_dma_chunk_buffer[LOG_TEMP_BUFFER_SIZE];

/* --------------------------- 内部函数声明 --------------------------- */
static void try_start_dma_transfer(void);

static uint16_t Log_ReadMessage(RingBuffer *rb, uint8_t *buffer, uint16_t buffer_size);

UART_HandleTypeDef array_of_uart_handles[4] = {NULL};

/* --------------------------- 公共函数实现 --------------------------- */

bool dma_logger_init(UART_HandleTypeDef *huart) {
    if (huart == NULL) {
        return false;
    }
    g_log_uart = huart;

    if (!CreateRingBuffer(&g_log_ring_buffer, LOG_RING_BUFFER_SIZE)) {
        return false;
    }
    LOG_W("heap", "%uKB- %u空间还剩余 %u", MEMORY_POND_MAX_SIZE, LOG_RING_BUFFER_SIZE, query_remain_size());

    g_dma_tx_busy = false;
    return true;
}

void dma_printf(const char *format, ...) {
    if (g_log_uart == NULL) return;

    char temp_buffer[LOG_TEMP_BUFFER_SIZE];

    va_list args;
    va_start(args, format);
    int len = vsnprintf(temp_buffer, sizeof(temp_buffer), format, args);
    va_end(args);

    if (len <= 0) return;

    uint16_t msg_len = (uint16_t) len;
    uint16_t header_len = sizeof(uint16_t);


    if (RingBuffer_GetRemainSize(&g_log_ring_buffer) >= header_len + msg_len) {
        WriteRingBuffer(&g_log_ring_buffer, (uint8_t *) &msg_len, &header_len, false);
        WriteRingBuffer(&g_log_ring_buffer, (uint8_t *) temp_buffer, &msg_len, false);
    }


    try_start_dma_transfer();
}

/* --------------------------- 内部辅助和回调函数 --------------------------- */

static void try_start_dma_transfer(void) {
    if (g_dma_tx_busy || RingBuffer_GetUsedSize(&g_log_ring_buffer) == 0) {
        return;
    }

    g_dma_tx_busy = true;


    uint16_t read_len = Log_ReadMessage(&g_log_ring_buffer, g_dma_chunk_buffer, sizeof(g_dma_chunk_buffer));


    if (read_len > 0) {
        if (HAL_UART_Transmit_DMA(g_log_uart, g_dma_chunk_buffer, read_len) != HAL_OK) {
            // 如果启动失败，清除标志，以便下次重试
            g_dma_tx_busy = false;
        }
    } else {
        // 缓冲区里有数据，但不足一条完整消息
        g_dma_tx_busy = false;
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
    if (g_log_uart != NULL && huart->Instance == g_log_uart->Instance) {
        g_dma_tx_busy = false;
        try_start_dma_transfer();
    }
}

static uint16_t Log_ReadMessage(RingBuffer *rb, uint8_t *buffer, uint16_t buffer_size) {
    uint16_t msg_len = 0;
    uint16_t header_len = sizeof(uint16_t);


    if (!PeekRingBuffer(rb, (uint8_t *) &msg_len, &header_len, false)) {
        return 0;
    }


    if (msg_len > buffer_size) {
        // 消息太长，清空缓冲区以恢复
        while (RingBuffer_GetUsedSize(rb) > 0) {
            uint16_t dummy_len = 1;
            uint8_t dummy_byte;
            ReadRingBuffer(rb, &dummy_byte, &dummy_len, false);
        }
        return 0;
    }

    if (RingBuffer_GetUsedSize(rb) < header_len + msg_len) {
        return 0;
    }

    uint8_t dummy_header_buffer[2];
    ReadRingBuffer(rb, dummy_header_buffer, &header_len, false);
    ReadRingBuffer(rb, buffer, &msg_len, false);

    return msg_len;
}
