//
// 创建：yan，2025/12/7
//

#ifndef SMARTCLOCK_AT_H
#define SMARTCLOCK_AT_H

#include <stdbool.h>
#include <stdint.h>

#include "HFSM.h"
#include "RingBuffer.h"
#include "stm32f4xx_hal.h"

/* 1：RTOS 模式（队列+信号量）；0：裸机轮询模式 */
#ifndef AT_RTOS_ENABLE
#define AT_RTOS_ENABLE 1
#endif

/* 0：阻塞发送（HAL_UART_Transmit）；1：DMA 发送（HAL_UART_Transmit_DMA） */
#ifndef AT_TX_USE_DMA
#define AT_TX_USE_DMA 1
#endif

/* Core task 事件标志位 */
#define AT_FLAG_RX (1u << 0)
#define AT_FLAG_TX (1u << 1)
#define AT_FLAG_TXDONE (1u << 2)

/* AT 缓冲区/限制参数 */
#define AT_RX_RB_SIZE 1024
#define AT_LEN_RB_SIZE 64
#define AT_DMA_BUF_SIZE 256
#define AT_LINE_MAX_LEN 256
#define AT_CMD_TIMEOUT_DEF 5000
#define AT_MAX_PENDING 16
/* MQTT 的 topic/鉴权/发布指令可能超过 128，预留一些空间。 */
#define AT_CMD_MAX_LEN 256
#define AT_EXPECT_MAX_LEN 64

#if AT_RTOS_ENABLE
#include "osal.h"
#endif

typedef struct AT_Manager_t AT_Manager_t;

typedef void (*AT_UrcCb)(AT_Manager_t *mgr, const char *line, void *user);
typedef bool (*HW_Send)(AT_Manager_t *mgr, const uint8_t *data, uint16_t len);

typedef enum {
    AT_RESP_OK = 0,
    AT_RESP_ERROR,
    AT_RESP_TIMEOUT,
    AT_RESP_BUSY,
    AT_RESP_WAITING,
} AT_Resp_t;

typedef enum {
    AT_EVT_NONE = 0,
    AT_EVT_SEND,
    AT_EVT_RX_LINE,
    AT_EVT_TIMEOUT,
} AT_EventID_t;

typedef enum {
    AT_TX_BLOCK = 0,
    AT_TX_DMA = 1,
} AT_TxMode;

typedef struct {
    char cmd_buf[AT_CMD_MAX_LEN];
    char expect_buf[AT_EXPECT_MAX_LEN];
    uint32_t timeout_ms;

    volatile AT_Resp_t result;

#if AT_RTOS_ENABLE
    osal_sem_t done_sem;
#else
    volatile bool is_finished;
#endif
    volatile uint8_t in_use;
} AT_Command_t;

typedef struct AT_Manager_t {
    StateMachine fsm;

    RingBuffer rx_rb;
    HW_Send hw_send;
    UART_HandleTypeDef *uart;

    uint8_t line_buf[AT_LINE_MAX_LEN];
    uint8_t dma_rx_arr[AT_DMA_BUF_SIZE];
    volatile uint16_t line_idx;
    volatile uint16_t isr_line_len;
    RingBuffer msg_len_rb;
    volatile uint16_t last_pos;
    volatile bool rx_overflow;
    void *urc_user;
    AT_UrcCb urc_cb;

    AT_Command_t *curr_cmd;
    volatile uint32_t req_start_tick;

#if AT_RTOS_ENABLE
    osal_sem_t tx_done_sem;
    volatile uint8_t tx_busy;
    volatile uint8_t tx_error;
    AT_TxMode tx_mode;
#endif

#if AT_RTOS_ENABLE
    osal_msgq_t cmd_q;
    osal_thread_t core_task;
    osal_mutex_t pool_mutex;

    AT_Command_t cmd_pool[AT_MAX_PENDING];
    uint16_t free_stack[AT_MAX_PENDING];
    uint16_t free_top;

    uint32_t curr_deadline_tick;
#else
    bool is_locked;
#endif
} AT_Manager_t;

void AT_Core_Init(AT_Manager_t *at_manager, UART_HandleTypeDef *uart, HW_Send hw_send);
void AT_Core_RxCallback(AT_Manager_t *at_manager, const UART_HandleTypeDef *huart, uint16_t Size);
void AT_Core_Process(AT_Manager_t *at_manager);

AT_Resp_t AT_SendCmd(AT_Manager_t *at_manager, const char *cmd, const char *expect, uint32_t timeout_ms);

uint32_t AT_MsToTicks(uint32_t ms);
AT_Resp_t AT_Poll(AT_Command_t *h);
void AT_SetUrcHandler(AT_Manager_t *mgr, AT_UrcCb cb, void *user);

AT_Command_t *AT_Submit(AT_Manager_t *mgr, const char *cmd, const char *expect, uint32_t timeout_ms);
AT_Command_t *AT_SendAsync(AT_Manager_t *mgr, const char *cmd, const char *expect, uint32_t timeout_ms);

void AT_SemDrain(osal_sem_t sem);
uint32_t AT_TxTimeoutMs(AT_Manager_t *mgr, uint16_t len);
void AT_SetTxMode(AT_Manager_t *mgr, AT_TxMode mode);

#endif /* SMARTCLOCK_AT_H */
