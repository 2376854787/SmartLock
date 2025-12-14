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
    AT_Manager_t* mgr = (AT_Manager_t*)argument;

    for (;;) {
        // 用一个小超时周期，让我们有机会做超时检查（例如 10ms）
        const uint32_t flags = osThreadFlagsWait(AT_FLAG_RX | AT_FLAG_TX,
                                           osFlagsWaitAny,
                                           AT_MsToTicks(10));

        if (!(flags & 0x80000000u)) {
            if (flags & AT_FLAG_RX) {
                AT_Core_Process(mgr); // 仍复用你的拆帧逻辑
            }
        }

        // 1) 若无当前命令，尝试取队列下一条并发送
        if (mgr->curr_cmd == NULL && mgr->cmd_q) {
            AT_Command_t *next = NULL;
            if (osMessageQueueGet(mgr->cmd_q, &next, NULL, 0) == osOK && next) {
                mgr->curr_cmd = next;
                mgr->req_start_tick = osKernelGetTickCount();
                mgr->curr_deadline_tick = mgr->req_start_tick + AT_MsToTicks(next->timeout_ms);

                // 真正发送：先保持阻塞发送也可以（因为在 core task 内，不会阻塞提交者）
                if (mgr->hw_send) {
                    mgr->hw_send(mgr, (uint8_t*)next->cmd_buf, (uint16_t)strlen(next->cmd_buf));
                }
            }
        }

        // 2) 超时检查（由 core task 统一收敛）
        if (mgr->curr_cmd) {
            const uint32_t now = osKernelGetTickCount();
            // 处理 tick 回绕：用有符号差判断
            if ((int32_t)(now - mgr->curr_deadline_tick) >= 0) {
                AT_Command_t *c = mgr->curr_cmd;
                c->result = AT_RESP_TIMEOUT;
                mgr->curr_cmd = NULL;
                osSemaphoreRelease(c->done_sem);

                // 超时后立刻尝试发下一条（提高吞吐）
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
 *
 * @param at at句柄
 * @param data 要发送的数据
 * @param size  数据大小 KB
 */
void Uart_send(AT_Manager_t *at, const uint8_t *data, const uint16_t size) {
    HAL_UART_Transmit(at->uart, data, size, HAL_MAX_DELAY);
}
