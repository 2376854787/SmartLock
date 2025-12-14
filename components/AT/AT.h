//
// Created by yan on 2025/12/7.
//

#ifndef SMARTCLOCK_AT_H
#define SMARTCLOCK_AT_H
#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_uart.h"
#include "HFSM.h"
#include "RingBuffer.h"
/* 1: 启用RTOS模式(信号量/互斥锁)  0: 启用裸机模式(轮询) */
#ifndef AT_RTOS_ENABLE
#define AT_RTOS_ENABLE      1    /*是否启用了RTOS*/

#endif
#define AT_FLAG_RX   (1u << 0)
#define AT_FLAG_TX   (1u << 1)
/* AT指令超时设置 */
#define AT_RX_RB_SIZE       1024        /* AT接收环形缓冲区大小 最好为2的幂*/
#define AT_LEN_RB_SIZE      64          /* 长度缓冲区: 存每行的长度 (存32行足够了, 32*2byte=64) */
#define AT_DMA_BUF_SIZE     256         /* DMA 接收缓冲区 */
#define AT_LINE_MAX_LEN     256         /* 单行回复最大长度 */
#define AT_CMD_TIMEOUT_DEF  5000        /* 默认超时时间 5s */
#define AT_MAX_PENDING      16          /* 同同一个串口最大排队的命令数 */
#define AT_CMD_MAX_LEN      128         /* 命令缓存长度  */
#define AT_EXPECT_MAX_LEN   64          /* expect 缓存长度 */

/* 根据模式引入头文件 */
#if AT_RTOS_ENABLE
#include "cmsis_os2.h"
#endif
/* 向前声明 */
typedef struct AT_Manager_t AT_Manager_t;

typedef void (*AT_UrcCb)(AT_Manager_t *mgr, const char *line, void *user);

typedef void (*HW_Send)(AT_Manager_t *at_manager, const uint8_t *data, uint16_t len);

/* ================= 枚举定义 ================= */
/* AT命令执行返回的结果 */
typedef enum {
    AT_RESP_OK = 0, /* 收到了期待的回复 */
    AT_RESP_ERROR, /*收到了“Error” */
    AT_RESP_TIMEOUT, /* 系统超时没有回复 */
    AT_RESP_BUSY, /* 系统忙 */
    AT_RESP_WAITING /* (内部状态) 正在等待中 */
} AT_Resp_t;

/* 内部事件 ID （用于驱动 HFSM）*/
typedef enum {
    AT_EVT_NONE = 0,
    AT_EVT_SEND, /* [操作] 请求发送 */
    AT_EVT_RX_LINE, /* [中断/轮询] 收到了一行完整数据 */
    AT_EVT_TIMEOUT, /* [Tick] 定时器超时 */
} AT_EventID_t;

/* ================= 核心结构体 ================= */
/**
 * @brief AT指令对象 (通常由调用者在栈上临时创建)
 */
typedef struct {
    /* --- 基础信息 --- */
    char cmd_buf[AT_CMD_MAX_LEN]; /* 发送的指令: "AT+CGREG?" */
    char expect_buf[AT_EXPECT_MAX_LEN]; /* 期望的回复: "+CGREG: 1,1" 或 NULL */
    uint32_t timeout_ms; /* 超时时间 */

    /* --- 运行结果 --- */
    volatile AT_Resp_t result; /* 最终运行结果回填在这里 */

    /* --- 同步机制 (双模兼容) --- */
#if AT_RTOS_ENABLE
    osSemaphoreId_t done_sem; /* RTOS: 信号量，用于阻塞调用任务 */
#else
    volatile bool is_finished; /* 裸机: 完成标志位，用于 while 等待 */
#endif
    volatile uint8_t in_use;
} AT_Command_t;

typedef struct AT_Manager_t {
    /* --- 1. 继承状态机 --- */
    StateMachine fsm;

    /* --- 2. 硬件/底层资源 --- */
    RingBuffer rx_rb; /* 接收环形缓冲区 */
    /* 如果 RingBuffer 需要静态内存，可以在此定义，或外部注入 */
    HW_Send hw_send; /* 硬件发送函数指针 */
    UART_HandleTypeDef *uart;

    /* --- 3. 解析缓存 --- */
    uint8_t line_buf[AT_LINE_MAX_LEN]; /* 线性缓存，存放当前正在解析的一行 */
    uint8_t dma_rx_arr[AT_DMA_BUF_SIZE]; /* DMA接收缓冲区 */
    volatile uint16_t line_idx; /* 当前行处理位置 */
    volatile uint16_t isr_line_len; /* 当前行写入位置 */
    RingBuffer msg_len_rb; /* 接收行长度环形缓冲区 */
    volatile uint16_t last_pos;
    volatile bool rx_overflow; /* 溢出检测 */
    void *urc_user; /* 上下文指针 */
    AT_UrcCb urc_cb; /* URC回调函数指针*/


    /* --- 4. 运行时状态 --- */
    AT_Command_t *curr_cmd; /* 当前正在执行的指令 */
    volatile uint32_t req_start_tick; /* 指令开始发送的时间戳 */
    /* --- 5. 线程与同步 (双模兼容) --- */
#if AT_RTOS_ENABLE
    osMessageQueueId_t cmd_q; // 存 AT_Command_t* 指针
    osThreadId_t core_task; /* AT 解析核心任务句柄 */
    osMutexId_t pool_mutex; /* 命令池互斥锁 */

    AT_Command_t cmd_pool[AT_MAX_PENDING]; /* 真正的对象数组 */
    uint16_t free_stack[AT_MAX_PENDING]; /* 空闲对象索引栈 */
    uint16_t free_top; /* 栈顶位置 */

    uint32_t curr_deadline_tick; // 当前命令超时点（tick）
#else
    bool is_locked; /* 裸机: 简单的忙标志 */
#endif
} AT_Manager_t;

/* ================= API 声明 ================= */

/**
 * @brief 初始化 AT 核心框架
 * @param at_manager
 * @param uart 串口句柄
 * @param hw_send 硬件串口发送函数指针
 */
void AT_Core_Init(AT_Manager_t *at_manager, UART_HandleTypeDef *uart, HW_Send hw_send);

/**
 * @brief 接收数据回调 (放入串口接收中断)
 * @param at_manager
 * @param byte 收到的单个字节
 */
void AT_Core_RxCallback(AT_Manager_t *at_manager, const UART_HandleTypeDef *huart, uint16_t Size);

/**
 * @brief 核心轮询/处理函数
 * @note  RTOS模式: 放入 AT_Task 中运行
 * @note  裸机模式: 放入 main while(1) 中运行
 */
void AT_Core_Process(AT_Manager_t *at_manager);

/**
 * @brief 发送 AT 指令并等待结果 (阻塞式接口)
 * @param cmd 指令内容，如 "AT"
 * @param expect 期望回复，如 "OK" (NULL表示只等默认OK)
 * @param timeout_ms 超时时间(ms)
 * @return 执行结果
 */
AT_Resp_t AT_SendCmd(AT_Manager_t *at_manager, const char *cmd, const char *expect, uint32_t timeout_ms);

/**
 * @brief 将ms转换为心跳
 * @param ms 需要转换为心跳的ms
 * @return 返回心跳
 */
uint32_t AT_MsToTicks(uint32_t ms);

/**
 * @brief   返回当前句柄当前执行对象的进度状态
 * @param h AT设备句柄
 * @return 对象的进度状态
 */
AT_Resp_t AT_Poll(AT_Command_t *h);

/**
 *
 * @param mgr AT设备句柄
 * @param cb  绑定的URC回调函数
 * @param user 传递的上下文
 */
void AT_SetUrcHandler(AT_Manager_t *mgr, AT_UrcCb cb, void *user);
#endif //SMARTCLOCK_AT_H
