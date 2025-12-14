#include "AT_Core_Task.h"

#include <stdio.h>
#include <string.h>


/* 任务句柄 */
osThreadId_t AT_Core_Task_Handle = NULL;

/* 引用外部变量 */
AT_Manager_t g_at_manager;

void Uart_send(AT_Manager_t *, const uint8_t *, uint16_t);

/**
 * @brief AT核心任务
 * @param argument 参数
 * @note 负责接收通知然后处理接收的数据
 */
void AT_Core_Task(void *argument) {
    AT_Manager_t *mgr = (AT_Manager_t *) argument;

    for (;;) {
        /*  用一个小超时周期，让我们有机会做超时检查（例如 10ms）*/
        const uint32_t flags = osThreadFlagsWait(AT_FLAG_RX | AT_FLAG_TX,
                                                 osFlagsWaitAny,
                                                 AT_MsToTicks(10));

        if (!(flags & 0x80000000u)) {
            if (flags & AT_FLAG_RX) {
                AT_Core_Process(mgr); // 仍复用你的拆帧逻辑
            }
        }

        /* 1、 若无当前命令，尝试取队列下一条并发送 */
        if (mgr->curr_cmd == NULL && mgr->cmd_q) {
            AT_Command_t *next = NULL;
            if (osMessageQueueGet(mgr->cmd_q, &next, NULL, 0) == osOK && next) {
                mgr->curr_cmd = next;
                mgr->req_start_tick = osKernelGetTickCount();
                mgr->curr_deadline_tick = mgr->req_start_tick + AT_MsToTicks(next->timeout_ms);

                if (mgr->hw_send) {
                    mgr->tx_error = 0;
                    mgr->hw_send(mgr, (uint8_t *) next->cmd_buf, (uint16_t) strlen(next->cmd_buf));

                    /* 未发生完成就返回 释放命令对象的信号量 重新通知任务发送 */
#if defined(AT_TX_USE_DMA) && (AT_TX_USE_DMA == 1)
                    if (mgr->tx_mode == AT_TX_DMA && mgr->tx_error) {
                        next->result = AT_RESP_ERROR;
                        mgr->curr_cmd = NULL;
                        osSemaphoreRelease(next->done_sem);
                        osThreadFlagsSet(mgr->core_task, AT_FLAG_TX); // 继续发下一条
                    }
#endif
                }
            }
        }

        /* 2、 超时检查（由 core task 统一收敛）*/
        if (mgr->curr_cmd) {
            const uint32_t now = osKernelGetTickCount();
            // 处理 tick 回绕：用有符号差判断
            if ((int32_t) (now - mgr->curr_deadline_tick) >= 0) {
                AT_Command_t *c = mgr->curr_cmd;
                c->result = AT_RESP_TIMEOUT;
                mgr->curr_cmd = NULL;
                osSemaphoreRelease(c->done_sem);

                /* 超时后立刻尝试发下一条（提高吞吐）*/
                osThreadFlagsSet(mgr->core_task, AT_FLAG_TX);
            }
        }
    }
}


/**
 * @brief 初初始化AT_回调处理任务
 */
void at_core_task_init(AT_Manager_t *at, UART_HandleTypeDef *uart) {
    const osThreadAttr_t AT_Task_attributes = {
        .name = "AT_Core_Task",
        .stack_size = 256 * 4,
        .priority = (osPriority_t) osPriorityNormal, /*  Normal，以免被低优先级日志阻塞 */
    };

    /* 1. 创建任务 */
    AT_Core_Task_Handle = osThreadNew(AT_Core_Task, at, &AT_Task_attributes);

    /* 2.立即赋值句柄，防止中断竞争 */
    if (AT_Core_Task_Handle != NULL) {
        at->core_task = AT_Core_Task_Handle;
    } else {
        LOG_E("AT_Task", "Task Create Failed!");
    }
    /* 3、继续初始化 */
    AT_Core_Init(at, uart, Uart_send);
    printf("AT core_task=%p\r\n", g_at_manager.core_task);
}

/**
 * @brief 提供DMA 和阻塞两种方式完成AT命令发送
 * @param mgr  AT设备句柄
 * @param data 要发送的数据指针
 * @param len 数据长度
 */
void Uart_send(AT_Manager_t *mgr, const uint8_t *data, uint16_t len) {
    if (!mgr || !mgr->uart || !data || len == 0) return;

#if defined(AT_TX_USE_DMA) && (AT_TX_USE_DMA == 1)
    if (mgr->tx_mode == AT_TX_DMA) {
        /* DMA 还在发就不要再启动 */
        if (mgr->tx_busy) {
            mgr->tx_error = 1;
            return;
        }

        mgr->tx_error = 0;
        mgr->tx_busy = 1;

        /* 不再等待回调 —— 异步 */
        if (HAL_UART_Transmit_DMA(mgr->uart, (uint8_t *) data, len) != HAL_OK) {
            mgr->tx_busy = 0;
            mgr->tx_error = 1;
        }
        return; /* 立刻返回 */
    }
#endif

    /* 阻塞模式 */
    (void) HAL_UART_Transmit(mgr->uart, (uint8_t *) data, len, HAL_MAX_DELAY);
}


/**
 * @brief 释放信号量
 * @param huart 串口句柄
 * @note DMA发送完成回调函数中调用
 */
void AT_Manage_TxCpltCallback(UART_HandleTypeDef *huart) {
    AT_Manager_t *mgr = AT_FindMgrByUart(huart);
    if (!mgr) return;

    mgr->tx_busy = 0;

    /* 唤醒任务 */
    if (mgr->core_task) {
        osThreadFlagsSet(mgr->core_task, AT_FLAG_TX);
    }
}
