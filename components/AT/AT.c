#include "APP_config.h"
#if (defined(CFG_FEAT_AT_SYSTEM) && (CFG_FEAT_AT_SYSTEM == 1))
#include <stdio.h>
#include <string.h>

#include "AT.h"
#include "MemoryAllocation.h"
#include "log.h"
#include "ret_code.h"

static void AT_OnLine(AT_Manager_t* mgr, const char* line);

AT_Resp_t AT_Wait(AT_Command_t* h, uint32_t wait_ms);

void AT_CmdRelease(AT_Manager_t* mgr, AT_Command_t* h);

#define AT_UART_RET(cls_, reason_) \
    RET_MAKE(RET_MOD_AT, RET_SUB_AT_TRANSPORT, RET_CODE_MAKE((cls_), (reason_)))
static void AT_UartEvtCb(void* user, const hal_uart_event_t* evt);
/**
 * @brief 将数据从线性数组 搬运到 span  包括获取span 搬运 提交
 * @param rb rb
 * @param src 数据目的地
 * @param want 想要写入的大小
 * @param isCompatible 兼容/严格模式
 * @param written 返回实际写入的大小
 * @return
 */
static ret_code_t AT_RbWriteSpscFromIsr(RingBuffer* rb, const uint8_t* src, uint32_t want,
                                        bool isCompatible, uint32_t* written) {
    if (written) *written = 0u;
    if (!rb || !src || want == 0u || !written)
        return AT_UART_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);

    RingBufferSpan span = {0};
    uint32_t granted    = 0u;
    /* 获取写入数据信息 */
    const ret_code_t rc = RingBuffer_WriteReserve_SPSC(rb, want, &span, &granted, isCompatible);
    if (ret_is_err(rc)) return rc;
    if (granted > 0u) {
        /* 指定数组数据 写入RB */
        RingBuffer_SpanWriteFromLinear(&span, src, granted);
        /* 提交实际写入的大小更改索引 */
        const ret_code_t commit_rc = RingBuffer_WriteCommit_SPSC(rb, granted);
        if (ret_is_err(commit_rc)) return commit_rc;
    }
    *written = granted;
    return RET_OK;
}
/**
 * @brief 将串口rb 的n1部分 搬运到 AT解析的rb 并解析是否有完整句子将其存储到 专门的rb
 * @param at_manager AT句柄
 * @param src  线性数组源
 * @param len  长度
 * @param has_line 返回是否解析到有一句完整的句子
 * @param stop 数据搬运完成 或者 数据搬运错误 返回true
 */
static void AT_ConsumeRxBlockFromIsr(AT_Manager_t* at_manager, const uint8_t* src, uint32_t len,
                                     bool* has_line, bool* stop) {
    if (!at_manager || !src || len == 0u || !stop || *stop) return;

    uint32_t written    = 0u;
    /* 搬运线性数组 数据 到rx_rb */
    ret_code_t write_rc = AT_RbWriteSpscFromIsr(&at_manager->rx_rb, src, len, true, &written);
    if (ret_is_err(write_rc)) {
        at_manager->rx_overflow = 1;
        *stop                   = true;
        return;
    }
    /* 挨个字节判断这个部分是否有一句完整的 句子 */
    for (uint32_t i = 0; i < written; i++) {
        const uint8_t b = src[i];
        ++(at_manager->isr_line_len);
        if (b == '\n' || b == '>') {
            /* 当前行的数据长度 */
            const uint16_t len_val     = at_manager->isr_line_len;
            const uint8_t len_bytes[2] = {(uint8_t)(len_val & 0xFFu),
                                          (uint8_t)((len_val >> 8) & 0xFFu)};
            uint32_t len_written       = 0u;
            /* 搬运线性数组 数据 到 msg_len_rb */
            write_rc = AT_RbWriteSpscFromIsr(&at_manager->msg_len_rb, len_bytes, sizeof(len_bytes),
                                             false, &len_written);
            at_manager->isr_line_len = 0;
            /* 结果错误/ 写入长度不等于数据大小 */
            if (ret_is_err(write_rc) || len_written != sizeof(len_bytes)) {
                at_manager->rx_overflow = 1;
                *stop                   = true;
                return;
            }
            if (has_line) *has_line = true;
        }
    }
    /* 实际搬运的数据小于想要搬运的 */
    if (written < len) {
        at_manager->rx_overflow = 1;
        *stop                   = true;
    }
}

/**
 * @brief 初始化串口 开启接收
 * @param at_device AT句柄
 * @param id 板级id
 * @param cfg 配置
 * @return
 */
static ret_code_t AT_StartHalUart(AT_Manager_t* at_device, hal_uart_id_t id,
                                  const hal_uart_cfg_t* cfg) {
    if (!at_device || !cfg) return AT_UART_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    /* 配置串口参数 */

    if (at_device->uart_hal) {
        (void)hal_uart_close(at_device->uart_hal);
        at_device->uart_hal = NULL;
    }

    /* 初始化串口 */
    ret_code_t rc = hal_uart_open(id, cfg, &at_device->uart_hal);
    if (ret_is_err(rc)) return rc;
    /* 设置串口回调函数 */
    rc = hal_uart_set_evt_cb(at_device->uart_hal, AT_UartEvtCb, at_device);
    if (ret_is_err(rc)) {
        (void)hal_uart_close(at_device->uart_hal);
        at_device->uart_hal = NULL;
        return rc;
    }

    /* 开启接受 */
    rc = hal_uart_rx_start(at_device->uart_hal);
    if (ret_is_err(rc)) {
        (void)hal_uart_close(at_device->uart_hal);
        at_device->uart_hal = NULL;
        return rc;
    }

    return RET_OK;
}

/**
 * @brief 解析由串口钩子触发后 读取的信息 找到完整一行并保存该句子的大小信息 以及信息搬运到 AT的句柄
 * RB 内
 * @param at_manager AT句柄
 * @param has_line 是否找到了完整的一句
 */
static void AT_DrainHalRxFromIsr(AT_Manager_t* at_manager, bool* has_line) {
    if (has_line) *has_line = false;
    if (!at_manager || !at_manager->uart_hal) return;

    bool stop = false;
    while (!stop) {
        hal_uart_read_span_t span = {0};
        uint32_t nread            = 0u;
        /* 申请串口接收缓冲区中的可读窗口 */
        const ret_code_t rc       = hal_uart_read_reserve(at_manager->uart_hal, 0u, &span, &nread);
        if (ret_is_err(rc) || nread == 0u) break;

        if (span.n1 > 0u) {
            /* 将串口rb 的n1部分 搬运到 AT解析的rb 并解析是否有完整句子将其存储到 专门的rb */
            AT_ConsumeRxBlockFromIsr(at_manager, span.p1, span.n1, has_line, &stop);
        }
        if (!stop && span.n2 > 0u) {
            /* 将串口rb 的n2部分 搬运到 AT解析的rb 并解析是否有完整句子将其存储到 专门的rb */
            AT_ConsumeRxBlockFromIsr(at_manager, span.p2, span.n2, has_line, &stop);
        }
        /* 提交实际读取的字节数 */
        (void)hal_uart_read_commit(at_manager->uart_hal, nread);
    }
}
/**
 * @brief 处理串口钩子 上报的事件
 * @param user 用户上下文
 * @param evt 串口向上汇报的事件
 */
static void AT_UartEvtCb(void* user, const hal_uart_event_t* evt) {
    /* 强转为 AT 句柄 */
    AT_Manager_t* at_manager = (AT_Manager_t*)user;
    if (!at_manager || !evt) return;
    /* 接收事件 IDLE 半满、全满 */
    if (evt->type == HAL_UART_EVT_RX) {
        bool has_line = false;
        /* 挨个字符解析成完整的一句 */
        AT_DrainHalRxFromIsr(at_manager, &has_line);
#if AT_RTOS_ENABLE
        /* 唤醒任务处理 */
        if (has_line && at_manager->core_task) {
            OSAL_thread_flags_set(at_manager->core_task, AT_FLAG_RX);
        }
#endif
        return;
    }

#if AT_RTOS_ENABLE
    /* 发送完成 */
    if (evt->type == HAL_UART_EVT_TX_DONE) {
        at_manager->tx_busy  = 0;
        at_manager->tx_error = 0;
        /* 释放信号量 */
        (void)OSAL_sem_give_from_isr(at_manager->tx_done_sem);
        if (at_manager->core_task) {
            /* 唤醒任务处理。继续发送可能有的下一个命令 */
            OSAL_thread_flags_set(at_manager->core_task, AT_FLAG_TXDONE);
        }
        return;
    }
    /*　串口发生错误　*/
    if (evt->type == HAL_UART_EVT_ERROR) {
        /* TX 路径错误：唤醒发送等待方和核心任务的发送状态机 */
        if (at_manager->tx_busy) {
            at_manager->tx_busy  = 0;
            at_manager->tx_error = 1;
            (void)OSAL_sem_give_from_isr(at_manager->tx_done_sem);
            if (at_manager->core_task) {
                OSAL_thread_flags_set(at_manager->core_task, AT_FLAG_TXDONE);
            }
            return;
        }

        /* RX 路径错误：标记溢出并唤醒解析线程执行止血重置 */
        at_manager->rx_overflow = 1;
        if (at_manager->core_task) {
            OSAL_thread_flags_set(at_manager->core_task, AT_FLAG_RX);
        }
    }
#endif
}

/**
 * @brief 初始化串口设备句柄初始化变量、消息队列、静态对象池
 * @param at_device 串口设备句柄
 * @param uart_id 串口板级id
 * @param uart_cfg 串口配置
 * @param hw_send   发送函数指针
 */
void AT_Core_Init(AT_Manager_t* at_device, hal_uart_id_t uart_id, const hal_uart_cfg_t* uart_cfg,
                  const HW_Send hw_send) {
    /* 1、接收发送命令函数指针 */
    at_device->hw_send = hw_send;

    /* 2、初始化AT管理的 RingBuffer缓冲区 */
    if (ret_is_err(CreateRingBuffer(&at_device->rx_rb, "at_device", AT_RX_RB_SIZE))) {
        LOG_E("RingBuffer", "at_device 环形缓冲区初始化失败");
    }
    LOG_W("heap", "%uKB- %u空间还剩余 %u", MEMORY_POND_MAX_SIZE, AT_RX_RB_SIZE,
          query_remain_size());

    if (ret_is_err(
            CreateRingBuffer(&at_device->msg_len_rb, "at_device.mesg_rx_rb", AT_LEN_RB_SIZE))) {
        LOG_E("RingBuffer", "at_device.mesg_rx_rb 环形缓冲区初始化失败");
    }
    LOG_W("heap", "%uKB- %u空间还剩余 %u", MEMORY_POND_MAX_SIZE, AT_LEN_RB_SIZE,
          query_remain_size());
    /* 3、初始化 HFSM 为空闲状态*/

    /* 4、初始化变量 */
    at_device->isr_line_len        = 0;
    at_device->curr_cmd            = NULL;
    at_device->urc_cb              = NULL;
    at_device->urc_user            = NULL;
    at_device->uart_hal            = NULL;
    at_device->uart_id             = uart_id;
    at_device->uart_baud           = (uart_cfg && uart_cfg->baud) ? uart_cfg->baud : 115200u;
    at_device->fsm.customizeHandle = at_device;
    at_device->fsm.fsm_name        = "AT";
    /* 有需求重新实现状态机 */
    LOG_I("AT", "Bind UART ID=%u", (unsigned)uart_id);

    /* 5、RTOS 裸机环境分开处理 */
#if AT_RTOS_ENABLE
    /* 句柄设置为休闲 */
    at_device->tx_busy  = 0;
    at_device->tx_error = 0;
    /* 初始化信号量 */
    OSAL_sem_create(&at_device->tx_done_sem, "ATDone", 0,
                    1);  // 初值0：等回调释放
    if (!at_device->tx_done_sem) {
        LOG_E("AT", "tx_done_sem create failed");
    }

    /* 默认初始化跟随全局设置 */
    at_device->tx_mode = (AT_TX_USE_DMA ? AT_TX_DMA : AT_TX_BLOCK);

    /*  创建队列（元素是 AT_Command_t*）*/
    OSAL_msgq_create(&at_device->cmd_q, "AT_CMD_Q", sizeof(AT_Command_t*), AT_MAX_PENDING);
    if (!at_device->cmd_q) {
        LOG_E("AT", "cmd_q create failed");
    }

    /*  创建池互斥（保护 alloc/free） */
    OSAL_mutex_create(&at_device->pool_mutex, "ATPool", true, true);
    if (!at_device->pool_mutex) {
        LOG_E("AT", "pool_mutex create failed");
    }

    /*  初始化 free 栈 + 预创建每个命令的 done_sem */
    at_device->free_top = 0;
    for (uint16_t i = 0; i < AT_MAX_PENDING; i++) {
        at_device->cmd_pool[i].in_use     = 0;
        at_device->cmd_pool[i].cancel_req = 0;
        at_device->cmd_pool[i].result     = AT_RESP_WAITING;
        at_device->cmd_pool[i].timeout_ms = AT_CMD_TIMEOUT_DEF;

        OSAL_sem_create(&at_device->cmd_pool[i].done_sem, "ATDone", 0, 1);
        if (!at_device->cmd_pool[i].done_sem) {
            LOG_E("AT", "done_sem create failed idx=%u", i);
        }

        at_device->free_stack[at_device->free_top++] = i;
    }
#else
    /* 裸机模式：简单复位标志位 */
    at_device->is_locked = false;
#endif
    {
        /* 初始化串口 并开启 DMA 接收 */
        const ret_code_t rc = AT_StartHalUart(at_device, uart_id, uart_cfg);
        if (ret_is_err(rc)) {
            LOG_E("AT", "hal uart start failed rc=%d", (int)rc);
        }
    }
    LOG_D("AT", "INIT at=%p core_task=%p\r\n", at_device, at_device->core_task);
}
/**
 * @brief 对串口接收处理断帧后的的数据进行处理
 * @param at_manager AT管理句柄
 */
void AT_Core_Process(AT_Manager_t* at_manager) {
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
        RingBufferSpan len_span = {0};
        uint32_t len_granted    = 0u;
        ret_code_t rc = RingBuffer_ReadReserve_SPSC(&at_manager->msg_len_rb, sizeof(uint16_t),
                                                    &len_span, &len_granted, false);
        if (ret_is_err(rc) || len_granted != sizeof(uint16_t)) {
            LOG_E("AT", "行读失败 rc=%d granted=%u", (int)rc, len_granted);
            break;
        }
        uint8_t len_size_t[2];
        RingBuffer_SpanReadToLinear(&len_span, len_size_t, sizeof(len_size_t));
        rc = RingBuffer_ReadCommit_SPSC(&at_manager->msg_len_rb, sizeof(uint16_t));
        if (ret_is_err(rc)) {
            LOG_E("AT", "行提交失败 rc=%d", (int)rc);
            break;
        }

        /* 4、当前行的长度 */
        const uint16_t frame_len = (uint16_t)len_size_t[0] | ((uint16_t)len_size_t[1] << 8);

        /* 限制最大读取数 */
        uint16_t actual          = frame_len;
        if (actual > (AT_LINE_MAX_LEN - 1)) {
            actual = (AT_LINE_MAX_LEN - 1);
        }

        /* 5、读取数据帧 */
        RingBufferSpan frame_span = {0};
        uint32_t to_read          = 0u;
        rc = RingBuffer_ReadReserve_SPSC(&at_manager->rx_rb, actual, &frame_span, &to_read, false);
        if (ret_is_err(rc) || (to_read != actual)) {
            LOG_E("AT", "数据帧读取失败/不同步 (need=%u got=%u rc=%d)", actual, to_read, (int)rc);
            break; /* 待实现重置策略 */
        }
        RingBuffer_SpanReadToLinear(&frame_span, at_manager->line_buf, actual);
        rc = RingBuffer_ReadCommit_SPSC(&at_manager->rx_rb, actual);
        if (ret_is_err(rc)) {
            LOG_E("AT", "数据帧提交失败 rc=%d", (int)rc);
            break; /* 待实现重置策略 */
        }

        /*６、判断数据帧是否完整 丢弃无法读取的*/
        if (frame_len > actual) {
            LOG_E("AT", "数据帧过长尝试丢弃数据 (can=%u fact=%u)", actual, frame_len);
            const uint16_t drop = frame_len - actual;
            if (ret_is_err(RingBuffer_ReadCommit_SPSC(&at_manager->rx_rb, drop))) {
                LOG_E("AT", "超长帧丢弃失败，数据可能已不同步");
            }
        }

        /* 7、加上结束符 */
        at_manager->line_buf[actual] = '\0';

        /* 8、开始状态机处理 */
        AT_OnLine(at_manager, (const char*)at_manager->line_buf);
        /* 打印返回数据 */
        LOG_W("AT", "RX: %s", at_manager->line_buf);
    }
}

/**
 * @brief 发送AT命令进入非阻塞处理流程
 * @param mgr 句柄
 * @param cmd 发送的命令
 * @param expect 期待收到的命令
 * @param timeout_ms 超时事件
 * @return 返回状态
 */
AT_Resp_t AT_SendCmd(AT_Manager_t* mgr, const char* cmd, const char* expect, uint32_t timeout_ms) {
#if !AT_RTOS_ENABLE
    return AT_RESP_ERROR;
#else

    AT_Command_t* h = AT_Submit(mgr, cmd, expect, timeout_ms);
    if (!h) return AT_RESP_BUSY;

    AT_Resp_t r = AT_Wait(h, h->timeout_ms);
    if (r == AT_RESP_TIMEOUT && h->result == AT_RESP_WAITING) {
        /* 等待超时只代表调用侧超时，不代表 core_task 已结束对该对象的引用。 */
        h->cancel_req = 1;
        if (mgr->core_task) {
            (void)OSAL_thread_flags_set(mgr->core_task, AT_FLAG_TX);
        }

        const uint32_t settle_ms   = (h->timeout_ms < 200u) ? h->timeout_ms : 200u;
        const ret_code_t settle_rc = OSAL_sem_take(h->done_sem, settle_ms);
        if (ret_is_ok(settle_rc)) {
            r = h->result;
        } else {
            r = AT_RESP_TIMEOUT;
            LOG_E("AT", "cmd settle timeout cmd=%s", h->cmd_buf);
        }
    }
    AT_CmdRelease(mgr, h);
    return r;
#endif
}

/**
 * @brief 处理断帧后的句子 进行期望字符串的模式匹配 匹配不上默认调用 URC回调
 * @param mgr AT设备句柄
 * @param line 返回的语句
 */
static void AT_OnLine(AT_Manager_t* mgr, const char* line) {
    if (!mgr || !line) return;

    /* 有正在执行的命令：优先作为响应处理 */
    if (mgr->curr_cmd) {
        AT_Command_t* c    = mgr->curr_cmd;
        const char* expect = (c->expect_buf[0] != '\0') ? c->expect_buf : "OK";

        if (strstr(line, expect)) {
            c->result     = AT_RESP_OK;
            mgr->curr_cmd = NULL;
            OSAL_sem_give(c->done_sem);
            // 触发发送下一条
            if (mgr->core_task) OSAL_thread_flags_set(mgr->core_task, AT_FLAG_TX);
            LOG_D("AT", "match result= AT_RESP_OK %d line=%s", c->result, line);
            return;
        }
        if (strstr(line, "ERROR")) {
            c->result     = AT_RESP_ERROR;
            mgr->curr_cmd = NULL;
            OSAL_sem_give(c->done_sem);
            if (mgr->core_task) OSAL_thread_flags_set(mgr->core_task, AT_FLAG_TX);
            LOG_D("AT", "match result= AT_RESP_ERROR %d line=%s", c->result, line);
            return;
        }
        if (strstr(line, "busy p") || strstr(line, "busy s")) {
            c->result     = AT_RESP_BUSY;
            mgr->curr_cmd = NULL;
            OSAL_sem_give(c->done_sem);
            if (mgr->core_task) OSAL_thread_flags_set(mgr->core_task, AT_FLAG_TX);
            LOG_D("AT", "match result= AT_RESP_BUSY %d line=%s", c->result, line);
            return;
        }

        /* 2、未命中：很可能是 URC/中间行，交给 URC 回调（如果有）*/
        if (mgr->urc_cb) {
            mgr->urc_cb(mgr, line, mgr->urc_user);
        }
        return;
    }

    /* 3、 没有 curr_cmd：一定是 URC */
    if (mgr->urc_cb) {
        mgr->urc_cb(mgr, line, mgr->urc_user);
    } else {
        LOG_W("AT", "URC: %s", line);
    }
}

/**
 * @brief 返回静态对象池中的一个空闲命令对象
 * @param mgr AT设备句柄
 * @return 返回空闲命令对象
 */
static AT_Command_t* AT_CmdAlloc(AT_Manager_t* mgr) {
    if (!mgr) return NULL;
#if AT_RTOS_ENABLE
    /* 获取锁 */
    if (mgr->pool_mutex) OSAL_mutex_lock(mgr->pool_mutex, OSAL_WAIT_FOREVER);
    /* 如果没有空闲对象 */
    if (mgr->free_top == 0) {
        if (mgr->pool_mutex) OSAL_mutex_unlock(mgr->pool_mutex);
        return NULL;
    }
    /* 获取在池中的位置 */
    const uint16_t idx = mgr->free_stack[--mgr->free_top];
    /* 根据索引返回空闲对象指针 */
    AT_Command_t* c    = &mgr->cmd_pool[idx];
    /* 当前对象在内存池中标记为被使用 */
    c->in_use          = 1;

    /* 释放锁 */
    if (mgr->pool_mutex) OSAL_mutex_unlock(mgr->pool_mutex);
    return c;
#else
    return NULL;
#endif
}

/**
 * @brief     将内存池中的对象进行释放重置参数
 * @param mgr AT句柄
 * @param c   要发送的数据句柄
 */
static void AT_CmdFree(AT_Manager_t* mgr, AT_Command_t* c) {
#if AT_RTOS_ENABLE
    if (!mgr || !c) return;
    /* 判断指针范围是否在池中 */
    if (c < mgr->cmd_pool || c >= &mgr->cmd_pool[AT_MAX_PENDING]) {
        LOG_E("AT", "CmdFree invalid ptr=%p", c);
        return;
    }
    /* 加锁：保护 in_use 检查、对象重置和 free 栈回收的原子性 */
    if (mgr->pool_mutex) OSAL_mutex_lock(mgr->pool_mutex, OSAL_WAIT_FOREVER);

    /* 防止重复释放 */
    if (c->in_use == 0) {
        if (mgr->pool_mutex) OSAL_mutex_unlock(mgr->pool_mutex);
        LOG_E("AT", "CmdFree double free idx=%u", (unsigned)(c - mgr->cmd_pool));
        return;
    }
    // 清理字段（保留 done_sem）
    c->in_use          = 0;
    c->cancel_req      = 0;
    c->result          = AT_RESP_WAITING;
    c->timeout_ms      = AT_CMD_TIMEOUT_DEF;
    c->cmd_buf[0]      = '\0';
    c->expect_buf[0]   = '\0';
    /* 指针相减计算元素索引 */
    const uint16_t idx = (uint16_t)(c - mgr->cmd_pool);
    /* 计算 c 在com_pool是第几个元素 */
    if (mgr->free_top < AT_MAX_PENDING) {
        mgr->free_stack[mgr->free_top++] = idx;
    } else {
        LOG_E("AT", "free_stack overflow (double free?) idx=%u", idx);
    }
    /* 释放锁 */
    if (mgr->pool_mutex) OSAL_mutex_unlock(mgr->pool_mutex);
#else
    (void)mgr;
    (void)c;
#endif
}

/**
 * @brief 获取信号量确保发送后只能被被数据接收到唤醒
 * @param sem 需要被获取的信号量
 * @note 死等
 */
void AT_SemDrain(osal_sem_t sem) {
#if AT_RTOS_ENABLE
    if (!sem) return;
    while (OSAL_sem_take(sem, 0) == RET_OK) {
        /* drain */
    }
#endif
}

/**
 * @brief 获取空闲对象装填参数后返回
 * @param mgr AT句柄
 * @param cmd 发送的AT命令
 * @param expect 期待返回中应该有的字符串
 * @param timeout_ms 超时时间
 * @return 返回一个装填好的命令对象指针
 */
AT_Command_t* AT_Submit(AT_Manager_t* mgr, const char* cmd, const char* expect,
                        uint32_t timeout_ms) {
#if !AT_RTOS_ENABLE
    (void)mgr;
    (void)cmd;
    (void)expect;
    (void)timeout_ms;
    return NULL;
#else
    /* 1、防止空指针 */
    if (!mgr || !cmd) return NULL;
    /* 2、设置默认超时时间 */
    if (timeout_ms == 0) timeout_ms = AT_CMD_TIMEOUT_DEF;

    /* 3、从静态池拿出其中一个空对象的指针 */
    AT_Command_t* c = AT_CmdAlloc(mgr);
    if (!c) return NULL;

    /* 4、尝试消耗掉掉信号量 保持默认没有信号量*/
    AT_SemDrain(c->done_sem);

    /* 5、拷贝 cmd，避免上层栈字符串悬空 */
    strncpy(c->cmd_buf, cmd, AT_CMD_MAX_LEN - 1);
    c->cmd_buf[AT_CMD_MAX_LEN - 1] = '\0';

    /* 6、期待字符串存在且其对应需要的缓冲区存在 */
    if (expect && expect[0]) {
        strncpy(c->expect_buf, expect, AT_EXPECT_MAX_LEN - 1);
        c->expect_buf[AT_EXPECT_MAX_LEN - 1] = '\0';
    } else {
        c->expect_buf[0] = '\0';  // 表示默认 OK
    }

    c->timeout_ms     = timeout_ms;
    c->cancel_req     = 0;
    c->result         = AT_RESP_WAITING;

    // 入队（队列满则归还）
    AT_Command_t* ptr = c;
    /* 消息队列获取失败释放命令*/
    if (OSAL_msgq_put(mgr->cmd_q, &ptr, 0) != RET_OK) {
        AT_CmdFree(mgr, c);
        return NULL;
    }

    // 唤醒 core_task：通知有新命令
    if (mgr->core_task) {
        OSAL_thread_flags_set(mgr->core_task, AT_FLAG_TX);
    }
    LOG_D("AT", "submit cmd=%s q=%p", c->cmd_buf, mgr->cmd_q);
    return c;
#endif
}

/**
 * @brief 非阻塞等待直到获取到信号量或者超时
 * @param h 命令对象指针
 * @param wait_ms 等待的时间
 * @return 返回
 */
AT_Resp_t AT_Wait(AT_Command_t* h, const uint32_t wait_ms) {
#if !AT_RTOS_ENABLE
    (void)h;
    (void)wait_ms;
    return AT_RESP_ERROR;
#else
    if (!h) return AT_RESP_ERROR;

    /* 非阻塞等待 */
    const ret_code_t st = OSAL_sem_take(h->done_sem, wait_ms);
    if (ret_is_ok(st)) {
        return h->result;
    }
    if (ret_is_timeout(st)) {
        /* 理论上 core_task 会在超时时释放 done_sem；这里 st!=OK 意味着系统异常 */
        return AT_RESP_TIMEOUT;
    }
    return AT_RESP_TIMEOUT;
#endif
}

/**
 *@brief  将内存池中的对象进行释放重置参数
 * @param mgr AT设备对象指针
 * @param h   AT命令对象指针
 */
void AT_CmdRelease(AT_Manager_t* mgr, AT_Command_t* h) {
#if AT_RTOS_ENABLE
    if (!mgr || !h) return;
    if (mgr->curr_cmd == h) {
        LOG_E("AT", "CmdRelease denied: command still owned by core");
        return;
    }
    AT_CmdFree(mgr, h);
#else
    (void)mgr;
    (void)h;
#endif
}

/**
 * @brief   返回当前句柄当前执行对象的进度状态
 * @param h AT设备句柄
 * @return 对象的进度状态
 */
AT_Resp_t AT_Poll(AT_Command_t* h) {
    if (!h) return AT_RESP_ERROR;
    return h->result;
}

/**
 *
 * @param mgr AT设备句柄
 * @param cb  绑定的URC回调函数
 * @param user 传递的上下文
 */
void AT_SetUrcHandler(AT_Manager_t* mgr, const AT_UrcCb cb, void* user) {
    mgr->urc_cb   = cb;
    mgr->urc_user = user;
}

/**
 * @brief 获取空闲对象装填参数后返回
 * @param mgr AT句柄
 * @param cmd 发送的AT命令
 * @param expect 期待返回中应该有的字符串
 * @param timeout_ms 超时时间
 * @return 返回一个装填好的命令对象指针
 * @note  非阻塞版
 */
AT_Command_t* AT_SendAsync(AT_Manager_t* mgr, const char* cmd, const char* expect,
                           uint32_t timeout_ms) {
    return AT_Submit(mgr, cmd, expect, timeout_ms);
}

/**
 * @brief 根据波特率和发送的数据长度计算需要的时间
 * @param mgr AT设备句柄
 * @param len 发送数据的长度
 * @return 返回发送数据需要的数据时间
 */
uint32_t AT_TxTimeoutMs(AT_Manager_t* mgr, uint16_t len) {
    // 估算：1字节≈10bit（起始+8数据+停止），超时时间留余量
    const uint32_t baud = (mgr && mgr->uart_baud != 0u) ? mgr->uart_baud : 115200u;
    uint32_t ms         = (uint32_t)((uint64_t)len * 10u * 1000u / baud);
    if (ms < 5) ms = 5;
    return ms + 20;  // 额外裕量
}

/**
 * @brief 更改具体AT设备的发送模式
 * @param mgr AT设备句柄
 * @param mode 设定的模式
 */
void AT_SetTxMode(AT_Manager_t* mgr, AT_TxMode mode) {
    mgr->tx_mode = mode;
}

#endif


