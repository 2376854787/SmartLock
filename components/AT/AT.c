//
// Created by yan on 2025/12/7.
//
#include "AT_UartMap.h"


#include <stdio.h>
#include <string.h>

#include "log.h"
#include "MemoryAllocation.h"
#include "AT.h"


static void AT_OnLine(AT_Manager_t *mgr, const char *line);


AT_Resp_t AT_Wait(AT_Command_t *h, uint32_t wait_ms);

void AT_CmdRelease(AT_Manager_t *mgr, AT_Command_t *h);


/**
 * @brief 初始化串口设备句柄初始化变量、消息队列、静态对象池
 * @param at_device 串口设备句柄
 * @param uart      绑定的串口
 * @param hw_send   发送函数指针
 */
void AT_Core_Init(AT_Manager_t *at_device, UART_HandleTypeDef *uart, const HW_Send hw_send) {
    /* 1、接收发送命令函数指针 */
    at_device->hw_send = hw_send;

    /* 2、初始化AT管理的 RingBuffer缓冲区 */
    if (!CreateRingBuffer(&at_device->rx_rb, AT_RX_RB_SIZE)) {
        LOG_E("RingBuffer", "at_device 环形缓冲区初始化失败");
    }
    LOG_W("heap", "%uKB- %u空间还剩余 %u", MEMORY_POND_MAX_SIZE, AT_RX_RB_SIZE, query_remain_size());

    if (!CreateRingBuffer(&at_device->msg_len_rb, AT_LEN_RB_SIZE)) {
        LOG_E("RingBuffer", "at_device.mesg_rx_rb 环形缓冲区初始化失败");
    }
    LOG_W("heap", "%uKB- %u空间还剩余 %u", MEMORY_POND_MAX_SIZE, AT_LEN_RB_SIZE, query_remain_size());
    /* 3、初始化 HFSM 为空闲状态*/


    /* 4、初始化变量 */
    at_device->line_idx = 0;
    at_device->isr_line_len = 0;
    at_device->last_pos = 0;
    at_device->curr_cmd = NULL;
    at_device->urc_cb = NULL;
    at_device->urc_user = NULL;
    at_device->uart = uart;
    at_device->fsm.customizeHandle = at_device;
    at_device->fsm.fsm_name = "fsm";
    /* 有需求重新实现状态机 */
    LOG_I("AT", "Bind UART=%p Instance=%p", uart, uart->Instance);


    /* 5、RTOS 裸机环境分开处理 */
#if AT_RTOS_ENABLE

    at_device->tx_busy = 0;
    at_device->tx_error = 0;
#if defined(AT_TX_USE_DMA) && (AT_TX_USE_DMA == 1)
    at_device->tx_done_sem = osSemaphoreNew(1, 0, NULL); // 初值0：等回调释放
    if (!at_device->tx_done_sem) {
        LOG_E("AT", "tx_done_sem create failed");
    }
#endif


    /*让 HAL 回调能找到对应 mgr */
    at_device->uart = uart;
    /* 绑定串口-> at_device 的路径*/
    AT_BindUart(at_device, uart);

    /* 默认初始化跟随全局设置 */
    at_device->tx_mode = (AT_TX_USE_DMA ? AT_TX_DMA : AT_TX_BLOCK);


    /*  创建队列（元素是 AT_Command_t*）*/
    at_device->cmd_q = osMessageQueueNew(AT_MAX_PENDING, sizeof(AT_Command_t *), NULL);
    if (!at_device->cmd_q) {
        LOG_E("AT", "cmd_q create failed");
    }

    /*  创建池互斥（保护 alloc/free） */
    at_device->pool_mutex = osMutexNew(NULL);
    if (!at_device->pool_mutex) {
        LOG_E("AT", "pool_mutex create failed");
    }

    /*  初始化 free 栈 + 预创建每个命令的 done_sem */
    at_device->free_top = 0;
    for (uint16_t i = 0; i < AT_MAX_PENDING; i++) {
        at_device->cmd_pool[i].in_use = 0;
        at_device->cmd_pool[i].result = AT_RESP_WAITING;
        at_device->cmd_pool[i].timeout_ms = AT_CMD_TIMEOUT_DEF;

        at_device->cmd_pool[i].done_sem = osSemaphoreNew(1, 0, NULL);
        if (!at_device->cmd_pool[i].done_sem) {
            LOG_E("AT", "done_sem create failed idx=%u", i);
        }

        at_device->free_stack[at_device->free_top++] = i;
    }

    /* 3、开启串口DMA接收 */
    HAL_UARTEx_ReceiveToIdle_DMA(uart,
                                 at_device->dma_rx_arr,
                                 AT_DMA_BUF_SIZE);
#else
    /* 裸机模式：简单复位标志位 */

    at_manager->is_locked = false;
    /* 、开启串口DMA接收 */
    HAL_UARTEx_ReceiveToIdle_DMA(uart, at_device->dma_rx_arr, AT_DMA_BUF_SIZE);
#endif
    LOG_D("AT", "INIT at=%p core_task=%p\r\n", at_device, at_device->core_task);
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
        osThreadFlagsSet(at_manager->core_task, AT_FLAG_RX);
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
        // /* 放置事件处理 */
        // Event ev = {
        //     .event_id = AT_EVT_RX_LINE,
        //     .event_data = (void *) at_manager->line_buf
        // };
        // HFSM_HandleEvent(&at_manager->fsm, &ev);
        AT_OnLine(at_manager, (const char *) at_manager->line_buf);
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
AT_Resp_t AT_SendCmd(AT_Manager_t *mgr, const char *cmd, const char *expect, uint32_t timeout_ms) {
#if !AT_RTOS_ENABLE
    // 你裸机分支后续再做同样的队列化；先聚焦 RTOS
    return AT_RESP_ERROR;
#else
    AT_Command_t *h = AT_Submit(mgr, cmd, expect, timeout_ms);
    if (!h) return AT_RESP_BUSY;

    const AT_Resp_t r = AT_Wait(h, h->timeout_ms);
    AT_CmdRelease(mgr, h);
    return r;
#endif
}

/**
 * @brief 对返回的字符串进行处理
 * @param mgr AT设备句柄
 * @param line 返回的语句
 */
static void AT_OnLine(AT_Manager_t *mgr, const char *line) {
    if (!mgr || !line) return;

    /* 有正在执行的命令：优先作为响应处理 */
    if (mgr->curr_cmd) {
        AT_Command_t *c = mgr->curr_cmd;
        const char *expect = (c->expect_buf[0] != '\0') ? c->expect_buf : "OK";

        if (strstr(line, expect)) {
            c->result = AT_RESP_OK;
            mgr->curr_cmd = NULL;
            osSemaphoreRelease(c->done_sem);
            // 触发发送下一条
            if (mgr->core_task) osThreadFlagsSet(mgr->core_task, AT_FLAG_TX);
            return;
        }
        if (strstr(line, "ERROR")) {
            c->result = AT_RESP_ERROR;
            mgr->curr_cmd = NULL;
            osSemaphoreRelease(c->done_sem);
            if (mgr->core_task) osThreadFlagsSet(mgr->core_task, AT_FLAG_TX);
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
 * @brief 将ms转换为心跳
 * @param ms 需要转换为心跳的ms
 * @return 返回心跳
 */
uint32_t AT_MsToTicks(const uint32_t ms) {
#if AT_RTOS_ENABLE
    const uint32_t freq = osKernelGetTickFreq();
    uint64_t ticks = ((uint64_t) ms * freq + 999u) / 1000u;
    if (ticks > 0xFFFFFFFFu) ticks = 0xFFFFFFFFu;
    return (uint32_t) ticks;
#else
    return ms;
#endif
}

/**
 * @brief 返回静态对象池中的一个空闲命令对象
 * @param mgr AT设备句柄
 * @return 返回空闲命令对象
 */
static AT_Command_t *AT_CmdAlloc(AT_Manager_t *mgr) {
    if (!mgr) return NULL;
#if AT_RTOS_ENABLE
    /* 获取锁 */
    if (mgr->pool_mutex) osMutexAcquire(mgr->pool_mutex, osWaitForever);
    /* 如果空闲对象为空 */
    if (mgr->free_top == 0) {
        if (mgr->pool_mutex) osMutexRelease(mgr->pool_mutex);
        return NULL;
    }
    /* 获取在池中的位置 */
    const uint16_t idx = mgr->free_stack[--mgr->free_top];
    /* 根据索引返回空闲对象指针 */
    AT_Command_t *c = &mgr->cmd_pool[idx];
    /* 当前对象在内存池中标记为被使用 */
    c->in_use = 1;

    /* 释放锁 */
    if (mgr->pool_mutex) osMutexRelease(mgr->pool_mutex);
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
static void AT_CmdFree(AT_Manager_t *mgr, AT_Command_t *c) {
#if AT_RTOS_ENABLE
    if (!mgr || !c) return;
    /* 判断指针范围是否在池中 */
    if (c < mgr->cmd_pool || c >= &mgr->cmd_pool[AT_MAX_PENDING]) {
        LOG_E("AT", "CmdFree invalid ptr=%p", c);
        return;
    }

    /* 防止重复释放 */
    if (c->in_use == 0) {
        LOG_E("AT", "CmdFree double free idx=%u", (unsigned)(c - mgr->cmd_pool));
        return;
    }
    // 清理字段（保留 done_sem）
    c->in_use = 0;
    c->result = AT_RESP_WAITING;
    c->timeout_ms = AT_CMD_TIMEOUT_DEF;
    c->cmd_buf[0] = '\0';
    c->expect_buf[0] = '\0';


    /* 加锁 */
    if (mgr->pool_mutex) osMutexAcquire(mgr->pool_mutex, osWaitForever);
    /* 计算索引 */
    const uint16_t idx = (uint16_t) (c - mgr->cmd_pool);
    /* 计算 c 在com_pool是第几个元素 */
    if (mgr->free_top < AT_MAX_PENDING) {
        mgr->free_stack[mgr->free_top++] = idx;
    } else {
        LOG_E("AT", "free_stack overflow (double free?) idx=%u", idx);
    }

    /* 释放锁 */
    if (mgr->pool_mutex) osMutexRelease(mgr->pool_mutex);
#else
    (void) mgr; (void) c;
#endif
}

/**
 * @brief 获取信号量确保发送后被任务唤醒
 * @param sem 需要被获取的信号量
 */
void AT_SemDrain(osSemaphoreId_t sem) {
#if AT_RTOS_ENABLE
    if (!sem) return;
    while (osSemaphoreAcquire(sem, 0) == osOK) {
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
AT_Command_t *AT_Submit(AT_Manager_t *mgr,
                        const char *cmd,
                        const char *expect,
                        uint32_t timeout_ms) {
#if !AT_RTOS_ENABLE
    (void) mgr;(void) cmd;(void) expect;(void) timeout_ms;
    return NULL;
#else
    /* 1、防止空指针 */
    if (!mgr || !cmd) return NULL;
    /* 2、设置默认超时时间 */
    if (timeout_ms == 0) timeout_ms = AT_CMD_TIMEOUT_DEF;

    /* 3、从静态池拿出其中一个空对象的指针 */
    AT_Command_t *c = AT_CmdAlloc(mgr);
    if (!c) return NULL;

    /* 4、获取掉信号量 */
    AT_SemDrain(c->done_sem);

    /* 5、拷贝 cmd，避免上层栈字符串悬空；可在这里统一补 */
    strncpy(c->cmd_buf, cmd, AT_CMD_MAX_LEN - 1);
    c->cmd_buf[AT_CMD_MAX_LEN - 1] = '\0';

    /* 6、期待字符串存在且其对应需要的缓冲区存在 */
    if (expect && expect[0]) {
        strncpy(c->expect_buf, expect, AT_EXPECT_MAX_LEN - 1);
        c->expect_buf[AT_EXPECT_MAX_LEN - 1] = '\0';
    } else {
        c->expect_buf[0] = '\0'; // 表示默认 OK
    }

    c->timeout_ms = timeout_ms;
    c->result = AT_RESP_WAITING;

    // 入队（队列满则归还）
    AT_Command_t *ptr = c;
    /* 消息队列获取失败释放命令*/
    if (osMessageQueuePut(mgr->cmd_q, &ptr, 0, 0) != osOK) {
        AT_CmdFree(mgr, c);
        return NULL;
    }

    // 唤醒 core_task：通知有新命令
    if (mgr->core_task) {
        osThreadFlagsSet(mgr->core_task, AT_FLAG_TX); // AT_FLAG_TX
    }

    return c;
#endif
}

/**
 *
 * @param h 命令对象指针
 * @param wait_ms 等待的时间
 * @return 返回
 */
AT_Resp_t AT_Wait(AT_Command_t *h, const uint32_t wait_ms) {
#if !AT_RTOS_ENABLE
    (void) h; (void) wait_ms;
    return AT_RESP_ERROR;
#else
    if (!h) return AT_RESP_ERROR;

    // 一般 wait_ms 用 h->timeout_ms 即可；这里允许上层额外控制
    const uint32_t ticks = (wait_ms == 0) ? osWaitForever : AT_MsToTicks(wait_ms);
    /* 阻塞等待 */
    const osStatus_t st = osSemaphoreAcquire(h->done_sem, ticks);

    if (st != osOK) {
        // 理论上 core_task 会在超时时释放 done_sem；这里 st!=OK 意味着系统异常
        return AT_RESP_TIMEOUT;
    }
    return h->result;
#endif
}

/**
 *@brief  将内存池中的对象进行释放重置参数
 * @param mgr AT设备对象指针
 * @param h   AT命令对象指针
 */
void AT_CmdRelease(AT_Manager_t *mgr, AT_Command_t *h) {
#if AT_RTOS_ENABLE
    if (!mgr || !h) return;
    AT_CmdFree(mgr, h);
#else
    (void) mgr; (void) h;
#endif
}

/**
 * @brief   返回当前句柄当前执行对象的进度状态
 * @param h AT设备句柄
 * @return 对象的进度状态
 */
AT_Resp_t AT_Poll(AT_Command_t *h) {
    if (!h) return AT_RESP_ERROR;
    return h->result; // WAITING/OK/ERROR/TIMEOUT
}

/**
 *
 * @param mgr AT设备句柄
 * @param cb  绑定的URC回调函数
 * @param user 传递的上下文
 */
void AT_SetUrcHandler(AT_Manager_t *mgr, const AT_UrcCb cb, void *user) {
    mgr->urc_cb = cb;
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
AT_Command_t *AT_SendAsync(AT_Manager_t *mgr, const char *cmd, const char *expect, uint32_t timeout_ms) {
    return AT_Submit(mgr, cmd, expect, timeout_ms);
}

/**
 * @brief 根据波特率和发送的数据长度计算需要的时间
 * @param mgr AT设备句柄
 * @param len 发送数据的长度
 * @return 返回发送数据需要的数据时间
 */
uint32_t AT_TxTimeoutMs(AT_Manager_t *mgr, uint16_t len) {
    // 估算：1字节≈10bit（起始+8数据+停止），超时时间留余量
    const uint32_t baud = (mgr && mgr->uart) ? mgr->uart->Init.BaudRate : 115200;
    uint32_t ms = (uint32_t) ((uint64_t) len * 10u * 1000u / baud);
    if (ms < 5) ms = 5;
    return ms + 20; // 额外裕量
}

/**
 * @brief 更改具体AT设备的发送模式
 * @param mgr AT设备句柄
 * @param mode 设定的模式
 */
void AT_SetTxMode(AT_Manager_t *mgr, AT_TxMode mode) {
    mgr->tx_mode = mode;
}
