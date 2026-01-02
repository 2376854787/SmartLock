/* 全局功能开关 */
#include "APP_config.h"
#ifdef ENABLE_AT_SYSTEM

#include "AT_Core_Task.h"

#include <stdio.h>
#include <string.h>

#include "log.h"
#include "mqtt_at_task.h"

/* 任务句柄 */
static osal_thread_t s_at_core_task_handle = NULL;
static osal_thread_t s_mqtt_task_handle = NULL;

/* 全局 AT 管理器实例 */
AT_Manager_t g_at_manager;

static bool Uart_send(AT_Manager_t *mgr, const uint8_t *data, uint16_t len);

static void AT_Core_Task(void *argument)
{
    AT_Manager_t *mgr = (AT_Manager_t *)argument;

    for (;;) {
        /* 用一个较短的等待窗口，便于周期性做超时检查。 */
        const uint32_t flags = OSAL_thread_flags_wait(AT_FLAG_RX | AT_FLAG_TX | AT_FLAG_TXDONE,
                                                      OSAL_FLAGS_WAIT_ANY,
                                                      10);

        if (!(flags & 0x80000000u)) {
            if (flags & AT_FLAG_RX) {
                AT_Core_Process(mgr);
            }
        }

        /* 出队并发送下一条 AT 指令 */
        if (mgr->curr_cmd == NULL && mgr->cmd_q) {
#if defined(AT_TX_USE_DMA) && (AT_TX_USE_DMA == 1)
            if (mgr->tx_mode == AT_TX_DMA && mgr->tx_busy) {
                goto timeout_check;
            }
#endif

            AT_Command_t *next = NULL;
            if (OSAL_msgq_get(mgr->cmd_q, &next, 0) == RET_OK && next) {
                mgr->curr_cmd = next;
                mgr->req_start_tick = OSAL_tick_get();
                mgr->curr_deadline_tick = mgr->req_start_tick + OSAL_ms_to_ticks(next->timeout_ms);
                LOG_D("AT", "deq cmd=%s", next->cmd_buf);

                if (mgr->hw_send) {
                    const bool ok = mgr->hw_send(mgr, (uint8_t *)next->cmd_buf, (uint16_t)strlen(next->cmd_buf));
                    LOG_D("AT", "send ok=%d busy=%u mode=%u", (int)ok, mgr->tx_busy, (unsigned)mgr->tx_mode);
                    if (!ok) {
                        next->result = AT_RESP_ERROR;
                        mgr->curr_cmd = NULL;
                        OSAL_sem_give(next->done_sem);
                        OSAL_thread_flags_set(mgr->core_task, AT_FLAG_TX);
                    }
                }
            }
        }

#if defined(AT_TX_USE_DMA) && (AT_TX_USE_DMA == 1)
    timeout_check:
#endif
        if (mgr->curr_cmd) {
            const uint32_t now = OSAL_tick_get();
            if ((int32_t)(now - mgr->curr_deadline_tick) >= 0) {
                AT_Command_t *c = mgr->curr_cmd;
                LOG_W("AT", "timeout cmd=%s", c->cmd_buf);
                c->result = AT_RESP_TIMEOUT;
                mgr->curr_cmd = NULL;
                OSAL_sem_give(c->done_sem);
                OSAL_thread_flags_set(mgr->core_task, AT_FLAG_TX);
            }
        }
    }
}

void at_core_task_init(AT_Manager_t *at, UART_HandleTypeDef *uart)
{
    const osal_thread_attr_t at_attr = {
        .name = "AT_Core_Task",
        .stack_size = 256 * 10,
        .priority = (osal_priority_t)OSAL_PRIO_NORMAL,
    };

    /* 创建 AT Core 任务 */
    if (ret_is_err(OSAL_thread_create(&s_at_core_task_handle, AT_Core_Task, at, &at_attr))) {
        LOG_E("AT_Task", "AT core task create failed");
    }
    if (s_at_core_task_handle != NULL) {
        at->core_task = s_at_core_task_handle;
    }

    /* 继续初始化：启动 DMA Idle RX 等 */
    AT_Core_Init(at, uart, Uart_send);
    printf("AT core_task=%p\r\n", g_at_manager.core_task);

    /* AT Core 就绪后再启动 MQTT 任务（WiFi + SNTP + 华为云 IoTDA MQTT）。 */
    if (s_mqtt_task_handle == NULL) {
        const osal_thread_attr_t mqtt_attr = {
            .name = "MQTT_AT",
            .stack_size = 1024 * 4,
            .priority = (osal_priority_t)OSAL_PRIO_NORMAL,
        };
        if (ret_is_err(OSAL_thread_create(&s_mqtt_task_handle, StartMqttAtTask, NULL, &mqtt_attr))) {
            LOG_E("MQTT", "MQTT task create failed");
            s_mqtt_task_handle = NULL;
        }
    }
}

static bool Uart_send(AT_Manager_t *mgr, const uint8_t *data, uint16_t len)
{
    if (!mgr || !mgr->uart || !data || len == 0) return false;

#if defined(AT_TX_USE_DMA) && (AT_TX_USE_DMA == 1)
    if (mgr->tx_mode == AT_TX_DMA) {
        if (mgr->tx_busy) return false;
        mgr->tx_busy = 1;
        if (HAL_UART_Transmit_DMA(mgr->uart, (uint8_t *)data, len) != HAL_OK) {
            mgr->tx_busy = 0;
            return false;
        }
        return true;
    }
#endif

    /* 阻塞发送：务必使用有限超时，避免 UART 异常时“整机卡死”。 */
    const uint32_t timeout_ms = AT_TxTimeoutMs(mgr, len);
    return (HAL_UART_Transmit(mgr->uart, (uint8_t *)data, len, timeout_ms) == HAL_OK);
}

void AT_Manage_TxCpltCallback(UART_HandleTypeDef *huart)
{
    AT_Manager_t *mgr = AT_FindMgrByUart(huart);
    if (!mgr) return;

    mgr->tx_busy = 0;
    if (mgr->core_task) {
        OSAL_thread_flags_set(mgr->core_task, AT_FLAG_TXDONE);
    }
}

#endif
