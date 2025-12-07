//
// Created by yan on 2025/12/7.
//

#ifndef SMARTCLOCK_AT_H
#define SMARTCLOCK_AT_H

/* 1: 启用RTOS模式(信号量/互斥锁)  0: 启用裸机模式(轮询) */
#ifndef AT_RTOS_ENABLE
#define AT_RTOS_ENABLE      1    /*是否启用了RTOS*/
#include "HFSM.h"
#include "RingBuffer.h"
#endif

/* AT指令超时设置 */
#define AT_RX_RB_SIZE       1024        /* AT接收环形缓冲区大小 最好为2的幂*/
#define AT_LINE_MAX_LEN     256         /* 单行回复最大长度 */
#define AT_CMD_TIMEOUT_DEF  5000        /* 默认超时时间 5s */


/* 根据模式引入头文件 */
#if AT_RTOS_ENABLE
#include "cmsis_os2.h"
#endif

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
    const char *cmd_str; /* 发送的指令: "AT+CGREG?" */
    const char *expect_resp; /* 期望的回复: "+CGREG: 1,1" 或 NULL */
    uint32_t timeout_ms; /* 超时时间 */

    /* --- 运行结果 --- */
    volatile AT_Resp_t result; /* 最终运行结果回填在这里 */

    /* --- 同步机制 (双模兼容) --- */
#if AT_RTOS_ENABLE
    osSemaphoreId_t resp_sem; /* RTOS: 信号量，用于阻塞调用任务 */
#else
    volatile bool is_finished; /* 裸机: 完成标志位，用于 while 等待 */
#endif
} AT_Command_t;

typedef struct {
    /* --- 1. 继承状态机 --- */
    StateMachine fsm;

    /* --- 2. 硬件/底层资源 --- */
    RingBuffer rx_rb; /* 接收环形缓冲区 */
    /* 如果 RingBuffer 需要静态内存，可以在此定义，或外部注入 */
    void (*hw_send)(uint8_t *data, uint16_t len); /* 硬件发送函数指针 */

    /* --- 3. 解析缓存 --- */
    char line_buf[AT_LINE_MAX_LEN]; /* 线性缓存，存放当前正在解析的一行 */
    uint16_t line_idx; /* 当前行写入位置 */

    /* --- 4. 运行时状态 --- */
    AT_Command_t *curr_cmd; /* 当前正在执行的指令 */
    uint32_t req_start_tick; /* 指令开始发送的时间戳 */

    /* --- 5. 线程与同步 (双模兼容) --- */
#if AT_RTOS_ENABLE
    osThreadId_t core_task; /* AT 解析核心任务句柄 */
    osMutexId_t send_mutex; /* 发送互斥锁 (保证多任务互斥) */
#else
    bool is_locked; /* 裸机: 简单的忙标志 */
#endif
} AT_Manager_t;

/* ================= API 声明 ================= */

/**
 * @brief 初始化 AT 核心框架
 * @param send_func 硬件串口发送函数指针
 */
void AT_Core_Init(void (*send_func)(uint8_t *, uint16_t));

/**
 * @brief 接收数据回调 (放入串口接收中断)
 * @param byte 收到的单个字节
 */
void AT_Core_RxCallback(uint8_t byte);

/**
 * @brief 核心轮询/处理函数
 * @note  RTOS模式: 放入 AT_Task 中运行
 * @note  裸机模式: 放入 main while(1) 中运行
 */
void AT_Core_Process(void);

/**
 * @brief 发送 AT 指令并等待结果 (阻塞式接口)
 * @param cmd 指令内容，如 "AT"
 * @param expect 期望回复，如 "OK" (NULL表示只等默认OK)
 * @param timeout 超时时间(ms)
 * @return 执行结果
 */
AT_Resp_t AT_SendCmd(const char *cmd, const char *expect, uint32_t timeout);
#endif //SMARTCLOCK_AT_H
