/* 全局启用配置宏 */
#include "APP_config.h"
#ifdef ENABLE_AT_SYSTEM
#include <stdio.h>
#include <string.h>

#include "AT_Core_Task.h"
/* 任务句柄 */
osal_thread_t AT_Core_Task_Handle = NULL;

/* 引用外部变量 */
AT_Manager_t g_at_manager;

bool Uart_send(AT_Manager_t *mgr, const uint8_t *data, uint16_t len);

/**
 * @brief AT核心任务
 * @param argument 参数
 * @note 负责接收通知然后处理接收的数据
 */
void AT_Core_Task(void *argument) {
    AT_Manager_t *mgr = (AT_Manager_t *)argument;

    for (;;) {
        /*  用一个小超时周期，让有机会做超时检查*/
        const uint32_t flags = OSAL_thread_flags_wait(AT_FLAG_RX | AT_FLAG_TX | AT_FLAG_TXDONE,
                                                      OSAL_FLAGS_WAIT_ANY, 10);

        if (!(flags & 0x80000000u)) {
            if (flags & AT_FLAG_RX) {
                AT_Core_Process(mgr);  // 拆帧逻辑
            }
        }

        /* 1、 若无当前命令，尝试取队列下一条并发送 */
        if (mgr->curr_cmd == NULL && mgr->cmd_q) {
            /* 判断是否能够发送 */
#if defined(AT_TX_USE_DMA) && (AT_TX_USE_DMA == 1)
            if (mgr->tx_mode == AT_TX_DMA && mgr->tx_busy) {
                /* DMA 还在发：不要取队列，不要动 next，不要判错 */
                /* 等待 TXDONE 或下一次 10ms 轮询 */
                goto timeout_check;
            }
#endif

            AT_Command_t *next = NULL;
            if (OSAL_msgq_get(mgr->cmd_q, &next, 0) == RET_OK && next) {
                mgr->curr_cmd           = next;
                mgr->req_start_tick     = OSAL_tick_get();
                mgr->curr_deadline_tick = mgr->req_start_tick + OSAL_ms_to_ticks(next->timeout_ms);
                LOG_D("AT", "deq cmd=%s", next->cmd_buf);
                /* 调用发送函数 */
                if (mgr->hw_send) {
                    /* 未发生完成就返回 释放命令对象的信号量 重新通知任务发送 */
                    const bool ok = mgr->hw_send(mgr, (uint8_t *)next->cmd_buf,
                                                 (uint16_t)strlen(next->cmd_buf));
                    LOG_D("AT", "send ok=%d busy=%u mode=%u", (int)ok, mgr->tx_busy,
                          (unsigned)mgr->tx_mode);
                    /* 异常处理 */
                    if (!ok) {
                        next->result  = AT_RESP_ERROR;
                        mgr->curr_cmd = NULL;
                        OSAL_sem_give(next->done_sem);
                        OSAL_thread_flags_set(mgr->core_task, AT_FLAG_TX); /* 继续发下一条 */
                    }
                }
            }
        }

        /* 2、 超时检查（由 core task 统一收敛）*/
    timeout_check:
        if (mgr->curr_cmd) {
            const uint32_t now = OSAL_tick_get();
            // 处理 tick 回绕：用有符号差判断
            if ((int32_t)(now - mgr->curr_deadline_tick) >= 0) {
                AT_Command_t *c = mgr->curr_cmd;
                c->result       = AT_RESP_TIMEOUT;
                mgr->curr_cmd   = NULL;
                OSAL_sem_give(c->done_sem);

                /* 超时后立刻尝试发下一条（提高吞吐）*/
                OSAL_thread_flags_set(mgr->core_task, AT_FLAG_TX);
            }
        }
    }
}

/**
 * @brief 初初始化AT_回调处理任务
 */
void at_core_task_init(AT_Manager_t *at, UART_HandleTypeDef *uart) {
    const osal_thread_attr_t at_attr = {
        .name       = "AT_Core_Task",
        .stack_size = 256 * 6,
        .priority   = (osal_priority_t)OSAL_PRIO_NORMAL, /*  Normal，以免被低优先级日志阻塞 */
    };

    /* 1. 创建任务 */
    OSAL_thread_create(&AT_Core_Task_Handle, AT_Core_Task, at, &at_attr);

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
bool Uart_send(AT_Manager_t *mgr, const uint8_t *data, uint16_t len) {
    if (!mgr || !mgr->uart || !data || len == 0) return false;

#if defined(AT_TX_USE_DMA) && (AT_TX_USE_DMA == 1)
    if (mgr->tx_mode == AT_TX_DMA) {
        if (mgr->tx_busy) return false;  // 忙不是“错误”，但启动失败就 false
        mgr->tx_busy = 1;
        if (HAL_UART_Transmit_DMA(mgr->uart, (uint8_t *)data, len) != HAL_OK) {
            mgr->tx_busy = 0;  // 失败回滚
            return false;
        }
        return true;  // DMA 已启动
    }
#endif

    return (HAL_UART_Transmit(mgr->uart, (uint8_t *)data, len, HAL_MAX_DELAY) == HAL_OK);
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
        OSAL_thread_flags_set(mgr->core_task, AT_FLAG_TXDONE);
    }
}

#endif
