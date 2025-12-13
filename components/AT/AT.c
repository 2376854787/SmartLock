//
// Created by yan on 2025/12/7.
//
#include "AT.h"

#include <stdio.h>
#include <string.h>

#include "log.h"
#include "MemoryAllocation.h"


// ====== 前置声明 ======
static bool AT_Idle_OnSend(StateMachine *fsm, const Event *e);

static bool AT_Wait_OnRxLine(StateMachine *fsm, const Event *e);

static bool AT_Wait_OnTimeout(StateMachine *fsm, const Event *e);

static void AT_IDLE_Enter(StateMachine *fsm);

static void AT_WAIT_Enter(StateMachine *fsm);

static void AT_WAIT_Exit(StateMachine *fsm);

/* ====== IDLE 状态事件表 ====== */
static const EventAction_t IDLE_actions[] = {
    {AT_EVT_SEND, AT_Idle_OnSend},
    {0, NULL} // 结束标记
};

/* ====== WAIT 状态事件表 ====== */
static const EventAction_t WAIT_actions[] = {
    {AT_EVT_RX_LINE, AT_Wait_OnRxLine},
    {AT_EVT_TIMEOUT, AT_Wait_OnTimeout},
    {0, NULL}
};

/* ====== 状态对象 ====== */
static const State AT_IDLE = {
    .state_name = "AT_IDLE",
    .on_enter = AT_IDLE_Enter,
    .on_exit = NULL,
    .event_actions = IDLE_actions,
    .parent = NULL
};

static const State AT_WAIT = {
    .state_name = "AT_WAIT",
    .on_enter = AT_WAIT_Enter,
    .on_exit = AT_WAIT_Exit,
    .event_actions = WAIT_actions,
    .parent = NULL
};


void AT_Core_Init(AT_Manager_t *at_manager, UART_HandleTypeDef *uart,
                  void (*hw_send)(AT_Manager_t *, const uint8_t *, uint16_t)) {
    /* 1、接收发送命令函数指针 */
    at_manager->hw_send = hw_send;

    /* 2、初始化AT管理的 RingBuffer缓冲区 */
    if (!CreateRingBuffer(&at_manager->rx_rb, AT_RX_RB_SIZE)) {
        LOG_E("RingBuffer", "g_AT_Manager 环形缓冲区初始化失败");
    }
    LOG_W("heap", "%uKB- %u空间还剩余 %u", MEMORY_POND_MAX_SIZE, AT_RX_RB_SIZE, query_remain_size());

    if (!CreateRingBuffer(&at_manager->msg_len_rb, AT_LEN_RB_SIZE)) {
        LOG_E("RingBuffer", "g_AT_Manager.mesg_rx_rb 环形缓冲区初始化失败");
    }
    LOG_W("heap", "%uKB- %u空间还剩余 %u", MEMORY_POND_MAX_SIZE, AT_LEN_RB_SIZE, query_remain_size());
    /* 3、初始化 HFSM 为空闲状态*/


    /* 4、初始化变量 */
    at_manager->line_idx = 0;
    at_manager->isr_line_len = 0;
    at_manager->last_pos = 0;
    at_manager->curr_cmd = NULL;
    at_manager->uart = uart;
    at_manager->fsm.customizeHandle = at_manager;
    at_manager->fsm.fsm_name = "fsm";

    HFSM_Init(&at_manager->fsm, &AT_IDLE);
    LOG_I("AT", "Bind UART=%p Instance=%p", uart, uart->Instance);


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
    at_manager->send_mutex = osMutexNew(&send_mutex_attr);

    if (at_manager->send_mutex == NULL) {
        /* 严重错误：互斥锁创建失败 (通常是Heap不够了) */
        LOG_E("AT", "Mutex Create Failed!");
    }
    /*　RTOS任务函数指针 */
    //at_manager->core_task = NULL;
    /* 3、开启串口DMA接收 */
    HAL_UARTEx_ReceiveToIdle_DMA(uart,
                                 at_manager->dma_rx_arr,
                                 AT_DMA_BUF_SIZE);
#else
    /* 裸机模式：简单复位标志位 */

    at_manager->is_locked = false;
    /* 、开启串口DMA接收 */
    HAL_UARTEx_ReceiveToIdle_DMA(uart, at_manager->dma_rx_arr, AT_DMA_BUF_SIZE);
#endif
    LOG_D("AT", "INIT at=%p core_task=%p\r\n", at_manager, at_manager->core_task);
}

/**
 *@brief  处理DMA的回调
 * @param at_manager
 * @param huart 串口句柄
 * @param Size  这次新增数据
 * @note  DMA + circle模式
 */
void AT_Core_RxCallback(AT_Manager_t *at_manager, const UART_HandleTypeDef *huart, uint16_t Size) {
    (void) Size;
    bool has_line = false;
    bool stop = false;
    /* 0. 句柄检查 */
    if (huart->Instance != at_manager->uart->Instance) return;

    /* 1. 计算 DMA 接收的数据量和位置 */
    /* 当前索引位置 */
    const uint16_t cur_pos = AT_DMA_BUF_SIZE - __HAL_DMA_GET_COUNTER(huart->hdmarx);

    /* 为空没有触发*/
    if (cur_pos == at_manager->last_pos) return;
    if (cur_pos > AT_DMA_BUF_SIZE) {
        LOG_E("AT", "DMA异常");
        return;
    }

    /* 新增长度 */
    uint16_t raw_len;
    /* 传输开始索引位置 */
    uint16_t start_index;

    // 计算本次接收数据的长度和起始索引
    if (cur_pos > at_manager->last_pos) {
        raw_len = cur_pos - at_manager->last_pos;
        start_index = at_manager->last_pos;
    } else {
        raw_len = AT_DMA_BUF_SIZE - at_manager->last_pos;
        start_index = at_manager->last_pos;
    }

    /* 2. 准备变量 */

    /* [静态变量] 记录当前行已接收的字节数 (跨中断保持) */


    /*  WriteRingBufferFromISR 需要传入指针，这里准备好 */
    uint16_t write_size_one = 1;

    /* 定义一个宏来处理单个字节逻辑，避免回卷代码重复 */
#define AT_HANDLE_BYTE(b) do { \
        /* A. 尝试写入 数据 RingBuffer */ \
        write_size_one = 1; \
        if (WriteRingBufferFromISR(&at_manager->rx_rb, &(b), &write_size_one, 0)) { \
            /* 只有写入成功才统计长度，防止 Buffer 满导致逻辑错位 */ \
            ++(at_manager->isr_line_len); \
            \
            /* B. 检测结束符 \n 或 > */ \
            if ((b) == '\n' || (b) == '>') { \
                /* 将当前行的长度 (uint16_t) 存入 长度 RingBuffer */ \
                uint16_t len_val = at_manager->isr_line_len; \
                uint16_t len_size = sizeof(uint16_t); \
                /* 注意：这里把 &len_val 强转为 uint8_t* 写入 2 个字节 */ \
               bool ok_len =  WriteRingBufferFromISR(&at_manager->msg_len_rb, (uint8_t*)&len_val, &len_size, 0); \
               at_manager->isr_line_len = 0; \
               /* 溢出 */    \
               if(!ok_len){ \
                 at_manager->rx_overflow = 1;   \
                 stop = true; \
               } else {       \
                 has_line = true; \
               }   \
            } \
        } \
    } while(0)

    /* 3. 第一段循环处理 */
    for (uint16_t i = 0; i < raw_len; i++) {
        uint8_t byte = at_manager->dma_rx_arr[start_index + i];
        AT_HANDLE_BYTE(byte);
        if (stop) break;
    }

    /* 4. 第二段循环处理 (处理 DMA 回卷情况: buffer尾 -> buffer头) */
    if (cur_pos < at_manager->last_pos) {
        for (uint16_t i = 0; i < cur_pos; i++) {
            uint8_t byte = at_manager->dma_rx_arr[i];
            AT_HANDLE_BYTE(byte);
            if (stop) break;
        }
    }

    /* 5. 更新位置 */
    at_manager->last_pos = cur_pos;

    /* 6. 通知任务 */
    if (has_line && at_manager->core_task) {
        osThreadFlagsSet(at_manager->core_task, 1u << 0);
    }
}


/**
 * @brief 对到来的数据进行处理
 * @param at_manager AT管理句柄
 */
void AT_Core_Process(AT_Manager_t *at_manager) {
    /* 处理写入失败 */
    if (at_manager->rx_overflow) {
        at_manager->rx_overflow = 0;
        ResetRingBuffer(&at_manager->msg_len_rb);
        ResetRingBuffer(&at_manager->rx_rb);
        at_manager->isr_line_len = 0;
        LOG_E("AT", "RB缓冲区写入失败");
    }
    /* 1、判断是否有一句完整的数据帧 */
    while (RingBuffer_GetUsedSize(&at_manager->msg_len_rb) >= sizeof(uint16_t)) {
        /* 2、 读取数据 */
        uint16_t size = sizeof(uint16_t);
        uint8_t len_size_t[2];

        /* 3、判读当前行的字节数 是否大于最大可读数 */
        if (!ReadRingBuffer(&at_manager->msg_len_rb, len_size_t, &size, 0)) {
            LOG_E("AT", "行读失败！");
            break;
        }
        if (size != sizeof(uint16_t)) {
            LOG_E("AT", "size被异常修改");
            break;
        }

        /* 4、当前行的长度 */
        const uint16_t frame_len = (uint16_t) len_size_t[0] | ((uint16_t) len_size_t[1] << 8);


        /* 限制最大读取数 */
        uint16_t actual = frame_len;
        if (actual > (AT_LINE_MAX_LEN - 1)) {
            actual = (AT_LINE_MAX_LEN - 1);
        }

        /* 5、读取数据帧 */
        uint16_t to_read = actual;
        if (!ReadRingBuffer(&at_manager->rx_rb, at_manager->line_buf, &to_read, 0) || to_read != actual) {
            LOG_E("AT", "数据帧读取失败/不同步 (need=%u got=%u)", actual, to_read);
            break; // 后续做“重置策略”
        }

        /*６、判断数据帧是否完整 丢弃无法读取的*/
        if (frame_len > actual) {
            LOG_E("AT", "数据帧过长尝试丢弃数据 (can=%u fact=%u)", actual, frame_len);
            uint16_t drop = frame_len - actual;
            while (drop > 0) {
                uint8_t dummy[32];
                uint16_t chunk = (drop > sizeof(dummy)) ? sizeof(dummy) : drop;
                if (!ReadRingBuffer(&at_manager->rx_rb, dummy, &chunk, 1) || chunk == 0) {
                    LOG_E("AT", "超长帧丢弃失败，数据可能已不同步");
                    break;
                }
                drop -= chunk;
            }
        }

        /* 7、加上结束符 */
        at_manager->line_buf[actual] = '\0';

        /* 8、开始状态机处理 */
        /* 放置事件处理 */
        Event ev = {
            .event_id = AT_EVT_RX_LINE,
            .event_data = (void *) at_manager->line_buf
        };
        HFSM_HandleEvent(&at_manager->fsm, &ev);
        /* 打印返回数据 */
        LOG_W("AT", "RX: %s", at_manager->line_buf);
    }
}

/**
 * @brief 发送AT命令进入状态机处理流程
 * @param mgr 句柄
 * @param cmd 发送的命令
 * @param expect 期待收到的命令
 * @param timeout_ms 超时事件
 * @return 返回状态
 */
AT_Resp_t AT_SendCmd(AT_Manager_t *mgr, const char *cmd, const char *expect, uint32_t timeout_ms) {
    if (!mgr || !cmd) return AT_RESP_ERROR;
    if (timeout_ms == 0) timeout_ms = AT_CMD_TIMEOUT_DEF;

#if AT_RTOS_ENABLE
    // 1) BUSY保护：一次只允许一个阻塞式命令在飞行中
    if (mgr->curr_cmd != NULL) return AT_RESP_BUSY;

    // 2) 发送互斥（可选但强烈建议：防止多任务同时调用SendCmd）
    if (mgr->send_mutex) {
        // 这里建议加一个超时，避免死锁
        if (osMutexAcquire(mgr->send_mutex, timeout_ms) != osOK) {
            return AT_RESP_BUSY;
        }
    }

    // 3) 栈上构造命令对象（阻塞期间一直有效）
    AT_Command_t cmd_obj = {0};
    cmd_obj.cmd_str = cmd;
    cmd_obj.expect_resp = expect;
    cmd_obj.timeout_ms = timeout_ms;
    cmd_obj.result = AT_RESP_WAITING;

    // 4) 创建二值信号量：初值0，最大1
    cmd_obj.resp_sem = osSemaphoreNew(1, 0, NULL);
    if (cmd_obj.resp_sem == NULL) {
        if (mgr->send_mutex) osMutexRelease(mgr->send_mutex);
        return AT_RESP_ERROR;
    }

    // 5) 投递“发送”事件：event_data必须是 &cmd_obj
    const Event ev = {
        .event_id = AT_EVT_SEND,
        .event_data = &cmd_obj
    };
    HFSM_HandleEvent(&mgr->fsm, &ev);

    // 6) 阻塞等待：OK/ERROR 会 Release；否则超时返回
    const osStatus_t st = osSemaphoreAcquire(cmd_obj.resp_sem, timeout_ms);

    // 7) 如果是超时：需要主动清理（因为WAIT里不会自动触发timeout事件）
    if (st != osOK) {
        cmd_obj.result = AT_RESP_TIMEOUT;

        // 强制清理当前命令，避免后续一直卡在WAIT
        mgr->curr_cmd = NULL;
        HFSM_Transition(&mgr->fsm, &AT_IDLE);
    }

    // 8) 销毁信号量
    osSemaphoreDelete(cmd_obj.resp_sem);

    // 9) 释放互斥
    if (mgr->send_mutex) osMutexRelease(mgr->send_mutex);

    return cmd_obj.result;

#else
    // ---- 裸机版本（轮询阻塞）----
    if (mgr->is_locked) return AT_RESP_BUSY;
    mgr->is_locked = true;

    AT_Command_t cmd_obj = {0};
    cmd_obj.cmd_str = cmd;
    cmd_obj.expect_resp = expect;
    cmd_obj.timeout_ms = timeout_ms;
    cmd_obj.result = AT_RESP_WAITING;
    cmd_obj.is_finished = false;

    Event ev = {.event_id = AT_EVT_SEND, .event_data = &cmd_obj};
    HFSM_HandleEvent(&mgr->fsm, &ev);

    uint32_t start = HAL_GetTick();
    while (!cmd_obj.is_finished) {
        AT_Core_Process(mgr); // 或者主循环里周期调
        if ((HAL_GetTick() - start) >= timeout_ms) {
            cmd_obj.result = AT_RESP_TIMEOUT;
            mgr->curr_cmd = NULL;
            HFSM_Transition(&mgr->fsm, &AT_IDLE);
            break;
        }
    }

    mgr->is_locked = false;
    return cmd_obj.result;
#endif
}


static void AT_IDLE_Enter(StateMachine *fsm) {
    (void) fsm;
    LOG_D("AT", "%s -> IDLE", fsm->fsm_name);
}

static void AT_WAIT_Enter(StateMachine *fsm) {
    (void) fsm;
    LOG_D("AT", "%s -> WAIT", fsm->fsm_name);
}

static void AT_WAIT_Exit(StateMachine *fsm) {
    (void) fsm;
    LOG_D("AT", "%s <- WAIT", fsm->fsm_name);
}

static bool AT_Idle_OnSend(StateMachine *fsm, const Event *e) {
    AT_Manager_t *mgr = (AT_Manager_t *) fsm->customizeHandle;
    if (!mgr || !e || !e->event_data) return true;

    AT_Command_t *cmd = (AT_Command_t *) e->event_data;

    // 绑定当前命令
    mgr->curr_cmd = cmd;
    mgr->curr_cmd->result = AT_RESP_WAITING;

    // 记录开始 tick（RTOS 推荐 osKernelGetTickCount；裸机可 HAL_GetTick）
#if AT_RTOS_ENABLE
    mgr->req_start_tick = osKernelGetTickCount();
#else
    mgr->req_start_tick = HAL_GetTick();
#endif

    // 发送
    if (mgr->hw_send) {
        mgr->hw_send(mgr, (uint8_t *) cmd->cmd_str, (uint16_t) strlen(cmd->cmd_str));
    }
    // 进入等待响应状态
    HFSM_Transition(fsm, &AT_WAIT);
    return true;
}

static bool AT_Wait_OnRxLine(StateMachine *fsm, const Event *e) {
    AT_Manager_t *mgr = (AT_Manager_t *) fsm->customizeHandle;
    if (!mgr || !mgr->curr_cmd || !e || !e->event_data) return true;

    const char *line = (const char *) e->event_data;
    AT_Command_t *cmd = mgr->curr_cmd;

    // 默认 expect：如果用户没给，就用 "OK"
    const char *expect = cmd->expect_resp ? cmd->expect_resp : "OK";

    if (strstr(line, expect)) {
        cmd->result = AT_RESP_OK;
#if AT_RTOS_ENABLE
        osSemaphoreRelease(cmd->resp_sem);
#else
        cmd->is_finished = true;
#endif
        mgr->curr_cmd = NULL;
        HFSM_Transition(fsm, &AT_IDLE);
        return true;
    }

    if (strstr(line, "ERROR")) {
        cmd->result = AT_RESP_ERROR;
#if AT_RTOS_ENABLE
        osSemaphoreRelease(cmd->resp_sem);
#else
        cmd->is_finished = true;
#endif
        mgr->curr_cmd = NULL;
        HFSM_Transition(fsm, &AT_IDLE);
        return true;
    }

    // 未命中：继续等待下一行
    return true;
}

static bool AT_Wait_OnTimeout(StateMachine *fsm, const Event *e) {
    (void) e;
    AT_Manager_t *mgr = (AT_Manager_t *) fsm->customizeHandle;
    if (!mgr || !mgr->curr_cmd) return true;

    AT_Command_t *cmd = mgr->curr_cmd;
    cmd->result = AT_RESP_TIMEOUT;

#if AT_RTOS_ENABLE
    osSemaphoreRelease(cmd->resp_sem);
#else
    cmd->is_finished = true;
#endif

    LOG_E("AT", "AT命令响应超时");
    mgr->curr_cmd = NULL;
    HFSM_Transition(fsm, &AT_IDLE);
    return true;
}
