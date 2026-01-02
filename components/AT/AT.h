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

/**
 * @brief 一条 AT 命令请求（入队 -> 发送 -> 等待响应 -> 完成）
 *
 * 说明：
 * - 上层调用 `AT_SendCmd()` / `AT_Submit()` 会从静态对象池分配一个 `AT_Command_t`，并把命令入队。
 * - AT Core 线程负责出队、发送、匹配响应（expect/ERROR/busy...），并最终释放 `done_sem`。
 * - cmd/expect 都会拷贝到本结构体里，避免上层栈字符串生命周期问题。
 */
typedef struct {
    /* 要发送的 AT 命令（建议以 \r\n 结尾）。 */
    char cmd_buf[AT_CMD_MAX_LEN];

    /* 期待匹配到的字符串：为空表示默认匹配 "OK"。 */
    char expect_buf[AT_EXPECT_MAX_LEN];

    /* 超时时间（ms）。 */
    uint32_t timeout_ms;

    /* 命令执行结果（由 AT Core 线程更新）。 */
    volatile AT_Resp_t result;

#if AT_RTOS_ENABLE
    /* 命令完成信号量：AT Core 线程在 OK/ERROR/TIMEOUT/BUSY 时释放。 */
    osal_sem_t done_sem;
#else
    /* 裸机模式下的完成标记（当前未使用）。 */
    volatile bool is_finished;
#endif

    /* 静态对象池占用标记（由 alloc/free 管理）。 */
    volatile uint8_t in_use;
} AT_Command_t;

/**
 * @brief AT 框架管理器（绑定一个 UART + 一套队列/对象池/解析状态）
 *
 * 说明：
 * - 发送：上层把 AT_Command 入队；AT Core 线程出队后通过 hw_send 发送。
 * - 接收：USART DMA + Idle 中断把“原始字节流”写入 rx_rb，并把每行长度写入 msg_len_rb；
 *         AT Core 线程读取一行后交给 `AT_OnLine()` 做 expect/ERROR/URC 分发。
 */
typedef struct AT_Manager_t {
    /* 状态机（目前主要承载句柄/名字等信息）。 */
    StateMachine fsm;

    /* ===== UART 绑定与底层发送 ===== */
    RingBuffer rx_rb;                 /* 接收 ringbuffer：保存原始字节流 */
    HW_Send hw_send;                  /* 发送函数指针（HAL_UART_Transmit 或 DMA） */
    UART_HandleTypeDef *uart;         /* 绑定的 UART（当前为 USART3） */

    /* ===== DMA 接收解析相关 ===== */
    uint8_t line_buf[AT_LINE_MAX_LEN];     /* 组包后的“一行字符串”缓冲 */
    uint8_t dma_rx_arr[AT_DMA_BUF_SIZE];   /* DMA 循环接收缓冲区 */
    volatile uint16_t line_idx;            /* 行缓冲写指针（保留字段） */
    volatile uint16_t isr_line_len;        /* ISR 中当前行累计长度 */
    RingBuffer msg_len_rb;                 /* 每行长度队列（uint16_t） */
    volatile uint16_t last_pos;            /* 上次处理到的 DMA 位置 */
    volatile bool rx_overflow;             /* ringbuffer 写入失败标记（需要复位） */

    /* ===== URC 回调 ===== */
    void *urc_user;                   /* URC 回调用户上下文 */
    AT_UrcCb urc_cb;                  /* URC 回调函数 */

    /* ===== 当前命令上下文 ===== */
    AT_Command_t *curr_cmd;           /* 当前正在等待响应的命令 */
    volatile uint32_t req_start_tick; /* 当前命令开始时间（tick） */

#if AT_RTOS_ENABLE
    /* ===== 发送控制（DMA 模式才使用） ===== */
    osal_sem_t tx_done_sem;           /* DMA 发送完成信号量（可选） */
    volatile uint8_t tx_busy;         /* 发送忙标记 */
    volatile uint8_t tx_error;        /* 发送错误标记（预留） */
    AT_TxMode tx_mode;                /* 发送模式：阻塞/ DMA */
#endif

#if AT_RTOS_ENABLE
    /* ===== RTOS 资源 ===== */
    osal_msgq_t cmd_q;                /* 命令队列（元素为 AT_Command_t*） */
    osal_thread_t core_task;          /* AT Core 线程句柄 */
    osal_mutex_t pool_mutex;          /* 对象池互斥锁 */

    /* ===== 静态对象池 ===== */
    AT_Command_t cmd_pool[AT_MAX_PENDING];
    uint16_t free_stack[AT_MAX_PENDING];   /* 空闲索引栈（LIFO） */
    uint16_t free_top;                     /* 栈顶 */

    /* 当前命令超时截止时间（tick）。 */
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
