//
// Created by yan on 2025/12/7.
//
#include "AT.h"

#include "log.h"
#include "MemoryAllocation.h"
#define  UART_RX_DMA_BUF_SIZE  256

static AT_Manager_t g_AT_Manager;
static State IDLE = {
    .state_name = "AT_IDLE",
    .event_actions = NULL,
    .on_enter = NULL,
    .on_exit = NULL,
    .parent = NULL /* 没有上层状态机 */
};


/* 中断回调使用的缓冲区 */
RingBuffer at_dma_rx_rb;
static uint16_t at_dma_rx_rb_pos = 0; /* 上一次的指针位置 */
extern DMA_HandleTypeDef hdma_usart3_rx;

void AT_Core_Init(UART_HandleTypeDef *uart, void (*send_func)(uint8_t *, uint16_t)) {
    /* 1、接收发送命令函数指针 */
    g_AT_Manager.hw_send = send_func;

    /* 2、初始化RingBuffer缓冲区 */
    if (!CreateRingBuffer(&g_AT_Manager.rx_rb, AT_RX_RB_SIZE)) {
        LOG_E("RingBuffer", "g_AT_Manager 环形缓冲区初始化失败");
    }
    LOG_W("heap", "%uKB- %u空间还剩余 %u", MEMORY_POND_MAX_SIZE, AT_RX_RB_SIZE, query_remain_size());

    if (!CreateRingBuffer(&at_dma_rx_rb, UART_RX_DMA_BUF_SIZE)) {
        LOG_E("RingBuffer", "AT_dma_rx_rb 环形缓冲区初始化失败");
    }
    LOG_W("heap", "%uKB- %u空间还剩余 %u", MEMORY_POND_MAX_SIZE, UART_RX_DMA_BUF_SIZE, query_remain_size());
    /* 3、初始化 HFSM 为空闲状态*/
    HFSM_Init(&g_AT_Manager.fsm, &IDLE);

    /* 4、初始化变量 */
    g_AT_Manager.line_idx = 0;
    g_AT_Manager.curr_cmd = NULL;
    g_AT_Manager.uart = uart;

    /* 5、RTOS 裸机环境分开处理 */
#if AT_RTOS_ENABLE
    /* 1. 定义互斥锁属性 (静态定义，保证属性结构体一直存在) */
    /* 属性：递归锁 + 优先级继承 */
    static const osMutexAttr_t send_mutex_attr = {
        .name = "AT_SendMutex",
        .attr_bits = osMutexRecursive | osMutexPrioInherit,
        .cb_mem = NULL,
        .cb_size = 0
    };

    /* 2. 创建互斥锁 */
    /* g_at_mgr 是你的全局管理器实例，或者通过参数传进来的指针 */
    g_AT_Manager.send_mutex = osMutexNew(&send_mutex_attr);

    if (g_AT_Manager.send_mutex == NULL) {
        /* 严重错误：互斥锁创建失败 (通常是Heap不够了) */
        LOG_E("AT", "Mutex Create Failed!");
    }

    /* 3、开启串口DMA接收 关闭半满中断*/
    HAL_UARTEx_ReceiveToIdle_DMA(uart,
                                 at_dma_rx_rb.buffer,
                                 UART_RX_DMA_BUF_SIZE);
    __HAL_DMA_DISABLE_IT(uart->hdmarx, DMA_IT_HT);
#else
    /* 裸机模式：简单复位标志位 */

    g_AT_Manager.is_locked = false;
    /* 、开启串口DMA接收 关闭半满中断*/
    HAL_UARTEx_ReceiveToIdle_DMA(uart,
                                 at_dma_rx_rb.buffer,
                                 UART_RX_DMA_BUF_SIZE);
    __HAL_DMA_DISABLE_IT(uart->hdmarx, DMA_IT_HT);
#endif
}

/**
 *
 * @param huart 串口句柄
 * @param Size  这次新增数据
 */
void AT_Core_RxCallback(UART_HandleTypeDef *huart, uint16_t Size) {
    /* 0. 安全检查: 确保是当前AT使用的串口 */
    if (huart->Instance != g_AT_Manager.uart->Instance) return;

    /* 1. 获取 DMA 当前写入位置 (也就是 buffer 里的索引) */
    /* 注意：CNDTR 寄存器是递减的，所以用 BufferSize 减去它得到当前 Index */
    uint16_t cur_pos = UART_RX_DMA_BUF_SIZE - __HAL_DMA_GET_COUNTER(huart->hdmarx);

    /* 2. 定义静态变量记录上次位置 */
    static uint16_t last_pos = 0;

    /* 3. 如果位置没变，说明是误触发，直接退出 */
    if (cur_pos == last_pos) return;

    /* 4. 计算并搬运数据 (核心: 处理回卷) */
    if (cur_pos > last_pos) {
        /* [情况A]: 线性模式 (未回卷)
         * DMA 写入区域: [last_pos] ---> [cur_pos]
         */
        uint16_t len = cur_pos - last_pos;
        WriteRingBufferFromISR(&g_AT_Manager.rx_rb, &at_dma_rx_rb.buffer[last_pos], &len, 0); // 假设你的Write接口第三个参数是长度指针
    } else {
        /* [情况B]: 回卷模式 (Wrap-Around)
         * DMA 写入区域 1: [last_pos] ---> [End]
         * DMA 写入区域 2: [Start]    ---> [cur_pos]
         */

        /* 第一段: 尾部数据 */
        uint16_t len_tail = UART_RX_DMA_BUF_SIZE - last_pos;
        WriteRingBufferFromISR(&g_AT_Manager.rx_rb, &at_dma_rx_rb.buffer[last_pos], &len_tail, 0);

        /* 第二段: 头部数据 */
        if (cur_pos > 0) {
            uint16_t len_head = cur_pos;
            WriteRingBufferFromISR(&g_AT_Manager.rx_rb, &at_dma_rx_rb.buffer[0], &len_head, 0);
        }
    }

    /* 5. 更新位置 */
    last_pos = cur_pos;
}
