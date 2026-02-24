#include "APP_config.h"
#if (defined(CFG_FEAT_AT_SYSTEM) && (CFG_FEAT_AT_SYSTEM == 1))

#include <string.h>

#include "AT_Core_Task.h"
#include "assert_cus.h"

osal_thread_t AT_Core_Task_Handle = NULL;
AT_Manager_t g_at_manager;

static bool Uart_send(AT_Manager_t* mgr, const uint8_t* data, uint16_t len);
static void AT_FinishCurrCmd(AT_Manager_t* mgr, AT_Resp_t result);
/**
 * @brief 取消该命令的发送并返回结果、再次唤醒任务
 * @param mgr AT句柄
 * @param result 返回结果
 */
static void AT_FinishCurrCmd(AT_Manager_t* mgr, const AT_Resp_t result) {
    ASSERT_PARAM(mgr != NULL);
    if (!mgr || !mgr->curr_cmd) return;
    AT_Command_t* c = mgr->curr_cmd;
    c->result       = result;
    mgr->curr_cmd   = NULL;
    (void)OSAL_sem_give(c->done_sem);
    if (mgr->core_task) {
        (void)OSAL_thread_flags_set(mgr->core_task, AT_FLAG_TX);
    }
}

void AT_Core_Task(void* argument) {
    AT_Manager_t* mgr = (AT_Manager_t*)argument;
    ASSERT_PARAM(mgr != NULL);
    if (mgr == NULL) return;
    for (;;) {
        const uint32_t flags = OSAL_thread_flags_wait(AT_FLAG_RX | AT_FLAG_TX | AT_FLAG_TXDONE,
                                                      OSAL_FLAGS_WAIT_ANY, 10);
        /* 接收事件 读取并解析 模式匹配 */
        if (flags & AT_FLAG_RX) {
            AT_Core_Process(mgr);
        }

        /* TXDONE：在异步TX模式下消耗TX补全/错误事件*/
#if defined(AT_TX_USE_DMA) && (AT_TX_USE_DMA == 1)
        if ((flags & AT_FLAG_TXDONE) && (mgr->tx_mode == AT_TX_DMA)) {
            if (mgr->tx_error && mgr->curr_cmd) {
                AT_FinishCurrCmd(mgr, AT_RESP_ERROR);
            }
            mgr->tx_error = 0;
        }
#endif

        /* 1、继续发送下一个命令 （当前命令为NULL 且 有内存池）*/
        if ((flags & AT_FLAG_TX) && mgr->curr_cmd == NULL && mgr->cmd_q) {
#if defined(AT_TX_USE_DMA) && (AT_TX_USE_DMA == 1)
            /* 异步发送模式 且 发送忙状态 */
            if (mgr->tx_mode == AT_TX_DMA && mgr->tx_busy) {
                goto timeout_check;
            }
#endif
            AT_Command_t* next = NULL;
            /* 获取下一个命令 */
            if (OSAL_msgq_get(mgr->cmd_q, &next, 0) == RET_OK && next) {
                if (next->cancel_req) {
                    AT_FinishCurrCmd(mgr, AT_RESP_TIMEOUT);
                    goto timeout_check;
                }
                mgr->curr_cmd           = next;
                mgr->req_start_tick     = OSAL_tick_get();
                /* 设置目标超时 tick */
                mgr->curr_deadline_tick = mgr->req_start_tick + OSAL_ms_to_ticks(next->timeout_ms);
                /* 发送AT命令 */
                if (mgr->hw_send) {
                    const bool ok =
                        mgr->hw_send(mgr, (uint8_t*)next->cmd_buf, (uint16_t)strlen(next->cmd_buf));
                    if (!ok) {
                        AT_FinishCurrCmd(mgr, AT_RESP_ERROR);
                    }
                }
            }
        }

    timeout_check:
        if (mgr->curr_cmd) {
            const uint32_t now = OSAL_tick_get();
            /* 检查超时 */
            if ((int32_t)(now - mgr->curr_deadline_tick) >= 0) {
                /* 设置超时状态转换以及 参数 */
                AT_FinishCurrCmd(mgr, AT_RESP_TIMEOUT);
            }
        }
    }
}

void at_core_task_init(AT_Manager_t* at) {
    ASSERT_PARAM(at != NULL);
    if (at == NULL) return;
    const osal_thread_attr_t at_attr = {
        .name       = "AT_Core_Task",
        .stack_size = 256 * 6,
        .priority   = (osal_priority_t)OSAL_PRIO_NORMAL,
    };

    OSAL_thread_create(&AT_Core_Task_Handle, AT_Core_Task, at, &at_attr);
    if (AT_Core_Task_Handle != NULL) {
        at->core_task = AT_Core_Task_Handle;
    }

    const hal_uart_cfg_t uart_cfg = {
        .baud         = AT_UART_BAUD,
        .data_bits    = AT_UART_DATA_BITS,
        .stop_bits    = AT_UART_STOP_BITS,
        .parity       = (uint8_t)AT_UART_PARITY,
        .flow_ctrl    = AT_UART_FLOW_CTRL,
        .isCompatible = true,
    };
    AT_Core_Init(at, AT_UART_PORT_ID, &uart_cfg, Uart_send);
}
/**
 * @brief 自定义的串口发送函数
 * @param mgr AT句柄
 * @param data 数据
 * @param len 长度
 * @return
 */
static bool Uart_send(AT_Manager_t* mgr, const uint8_t* data, uint16_t len) {
    /* 参数检查 */
    ASSERT_PARAM((mgr != NULL) && (mgr->uart_hal != NULL) && (data != NULL) && (len != 0u));
    if (!mgr || !mgr->uart_hal || !data || len == 0) return false;
    if (mgr->tx_busy) return false;
    /* 更新为忙状态 */
    mgr->tx_busy  = 1;
    mgr->tx_error = 0;
    /* 发送数据 */
    if (ret_is_err(hal_uart_send_async(mgr->uart_hal, data, (uint32_t)len))) {
        mgr->tx_busy = 0;
        return false;
    }

#if defined(AT_TX_USE_DMA) && (AT_TX_USE_DMA == 1)
    if (mgr->tx_mode == AT_TX_DMA) return true;
#endif
    /* 发送数据后等待信号量 */
    const ret_code_t wait_rc = OSAL_sem_take(mgr->tx_done_sem, AT_TxTimeoutMs(mgr, len));
    if (ret_is_ok(wait_rc) && mgr->tx_error == 0) return true;
    mgr->tx_busy  = 0;
    mgr->tx_error = 0;
    return false;
}

#endif


