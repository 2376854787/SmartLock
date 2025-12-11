//
// Created by yan on 2025/12/7.
//
#include "AT.h"

#include <string.h>

#include "log.h"
#include "MemoryAllocation.h"

static AT_Manager_t g_AT_Manager;
static const State IDLE = {
    .state_name = "AT_IDLE",
    .event_actions = NULL,
    .on_enter = NULL,
    .on_exit = NULL,
    .parent = NULL /* 没有上层状态机 */
};


/* 中断回调使用的缓冲区 */
static uint8_t at_dma_rx_arr[AT_DMA_BUF_SIZE];
static uint16_t at_dma_rx_rb_pos = 0; /* 上一次的指针位置 */
extern DMA_HandleTypeDef hdma_usart3_rx;

void AT_Core_Init(UART_HandleTypeDef *uart, void (*send_func)(uint8_t *, uint16_t)) {
    /* 1、接收发送命令函数指针 */
    g_AT_Manager.hw_send = send_func;

    /* 2、初始化AT管理的 RingBuffer缓冲区 */
    if (!CreateRingBuffer(&g_AT_Manager.rx_rb, AT_RX_RB_SIZE)) {
        LOG_E("RingBuffer", "g_AT_Manager 环形缓冲区初始化失败");
    }
    LOG_W("heap", "%uKB- %u空间还剩余 %u", MEMORY_POND_MAX_SIZE, AT_RX_RB_SIZE, query_remain_size());

    if (!CreateRingBuffer(&g_AT_Manager.msg_len_rb, AT_LEN_RB_SIZE)) {
        LOG_E("RingBuffer", "g_AT_Manager.mesg_rx_rb 环形缓冲区初始化失败");
    }
    LOG_W("heap", "%uKB- %u空间还剩余 %u", MEMORY_POND_MAX_SIZE, AT_LEN_RB_SIZE, query_remain_size());
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
    g_AT_Manager.core_task = NULL;
    /* 3、开启串口DMA接收 */
    HAL_UARTEx_ReceiveToIdle_DMA(uart,
                                 at_dma_rx_arr,
                                 AT_LEN_RB_SIZE);
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
/* AT.c */

void AT_Core_RxCallback(const UART_HandleTypeDef *huart, uint16_t Size) {
    /* 0. 句柄检查 */
    if (huart->Instance != g_AT_Manager.uart->Instance) return;

    /* 1. 计算 DMA 接收的数据量和位置 */
    /* 注意：Size 参数在 ReceiveToIdle 中断里表示接收到的字节数，
     * 但为了兼容不同 HAL 版本和处理回卷，推荐使用 CNT 寄存器计算 */
    const uint16_t cur_pos = AT_DMA_BUF_SIZE - __HAL_DMA_GET_COUNTER(huart->hdmarx);
    static uint16_t last_pos = 0;

    if (cur_pos == last_pos) return;

    uint16_t raw_len;
    uint16_t start_index;

    // 计算本次接收数据的长度和起始索引
    if (cur_pos > last_pos) {
        raw_len = cur_pos - last_pos;
        start_index = last_pos;
    } else {
        raw_len = AT_DMA_BUF_SIZE - last_pos;
        start_index = last_pos;
    }

    /* 2. 准备变量 */
    const uint8_t *dma_buf = at_dma_rx_arr;

    /* [静态变量] 记录当前行已接收的字节数 (跨中断保持) */
    static uint16_t current_line_len = 0;

    /*  WriteRingBufferFromISR 需要传入指针，这里准备好 */
    uint16_t write_size_one = 1;

    /* 定义一个宏来处理单个字节逻辑，避免回卷代码重复 */
#define AT_HANDLE_BYTE(b) do { \
        /* A. 尝试写入 数据 RingBuffer */ \
        write_size_one = 1; \
        if (WriteRingBufferFromISR(&g_AT_Manager.rx_rb, &(b), &write_size_one, 0)) { \
            /* 只有写入成功才统计长度，防止 Buffer 满导致逻辑错位 */ \
            current_line_len++; \
            \
            /* B. 检测结束符 \n 或 > */ \
            if ((b) == '\n' || (b) == '>') { \
                /* 将当前行的长度 (uint16_t) 存入 长度 RingBuffer */ \
                uint16_t len_val = current_line_len; \
                uint16_t len_size = sizeof(uint16_t); \
                /* 注意：这里把 &len_val 强转为 uint8_t* 写入 2 个字节 */ \
                WriteRingBufferFromISR(&g_AT_Manager.msg_len_rb, (uint8_t*)&len_val, &len_size, 0); \
                \
                /* 重置当前行计数 */ \
                current_line_len = 0; \
                \
                /* 通知任务 */ \
                if (g_AT_Manager.core_task != NULL) { \
                    osThreadFlagsSet(g_AT_Manager.core_task, 1u << 0); \
                } \
            } \
        } \
    } while(0)

    /* 3. 第一段循环处理 */
    for (uint16_t i = 0; i < raw_len; i++) {
        uint8_t byte = dma_buf[start_index + i];
        AT_HANDLE_BYTE(byte);
    }

    /* 4. 第二段循环处理 (处理 DMA 回卷情况: buffer尾 -> buffer头) */
    if (cur_pos < last_pos) {
        for (uint16_t i = 0; i < cur_pos; i++) {
            uint8_t byte = dma_buf[i];
            AT_HANDLE_BYTE(byte);
        }
    }

    /* 5. 更新位置 */
    last_pos = cur_pos;
}


/**
 * @brief 处理接收到的一行或则多行数据
 */
void AT_Core_Process(void) {
    uint16_t frame_len = 0;
    /* 每次我们要从 msg_len_rb 读取 2 个字节 (sizeof uint16_t) */
    uint16_t len_item_size = sizeof(uint16_t);

    /*
     * 循环处理：只要 长度RB 里有数据 (>= 2字节)，说明有一行完整数据待读
     */
    while (RingBuffer_GetUsedSize(&g_AT_Manager.msg_len_rb) >= sizeof(uint16_t)) {
        /* 1. 先读出这一行的长度 */
        /* ReadRingBufferFromISR/ReadRingBuffer 均可，Process在任务中建议用 ReadRingBuffer */
        /* 注意：这里必须重置 len_item_size，因为 API 会修改它 */
        len_item_size = sizeof(uint16_t);
        if (!ReadRingBuffer(&g_AT_Manager.msg_len_rb, (uint8_t *) &frame_len, &len_item_size, 0)) {
            break;
        }

        /* 2. 安全校验：防止溢出 line_buf */
        uint16_t actual_read_len = frame_len;
        if (actual_read_len > AT_LINE_MAX_LEN - 1) {
            actual_read_len = AT_LINE_MAX_LEN - 1;
            // 如果实际行超长，后续代码需要考虑如何丢弃多余数据，这里简单截断读取
        }

        /* 3. 从 数据RB 中精确读出该行数据 */
        // 因为 msg_len_rb 告诉我们这行有 frame_len 这么长，所以 rx_rb 里一定有这么多数据
        ReadRingBuffer(&g_AT_Manager.rx_rb, g_AT_Manager.line_buf, &actual_read_len, 0);

        /* 如果 frame_len > actual_read_len (截断情况)，需要把 rx_rb 里多余的读掉丢弃 */
        if (frame_len > actual_read_len) {
            uint16_t drop_len = frame_len - actual_read_len;
            uint8_t dummy;
            // 简单的丢弃循环
            for (int k = 0; k < drop_len; k++) {
                uint16_t s = 1;
                ReadRingBuffer(&g_AT_Manager.rx_rb, &dummy, &s, 0);
            }
        }

        /* 4. 补 0 形成字符串 */
        g_AT_Manager.line_buf[actual_read_len] = '\0';

        /* 5. 业务逻辑 (匹配) */
        if (g_AT_Manager.curr_cmd != NULL) {
            if (g_AT_Manager.curr_cmd->expect_resp != NULL &&
                strstr((char *) g_AT_Manager.line_buf, g_AT_Manager.curr_cmd->expect_resp)) {
                g_AT_Manager.curr_cmd->result = AT_RESP_OK;
#if AT_RTOS_ENABLE
                osSemaphoreRelease(g_AT_Manager.curr_cmd->resp_sem);
#endif
            } else if (strstr((char *) g_AT_Manager.line_buf, "ERROR")) {
                g_AT_Manager.curr_cmd->result = AT_RESP_ERROR;
#if AT_RTOS_ENABLE
                osSemaphoreRelease(g_AT_Manager.curr_cmd->resp_sem);
#endif
            }
        }
    }
}
