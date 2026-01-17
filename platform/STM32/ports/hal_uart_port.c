#include <stdio.h>
#include <string.h>

#include "RingBuffer.h"
#include "hal_uart.h"
#include "ret_code.h"
#include "stm32_hal.h"
#include "stm32_uart_bsp.h"
#include "stm32_uart_series.h"

/* 根据当前所属模块id 与返回状态生成32位状态码 */
#define UART_RET(errno_) RET_MAKE(RET_MOD_PORT, (errno_))

/* ========== UART 上下文 ========== */
struct hal_uart {
    const char *name; /*串口名称*/
    void *cb_user;
    hal_uart_id_t id;     /*串口内部id*/
    stm32_uart_bsp_t bsp; /* 串口映射配置 */

    RingBuffer rb;        /* 软件 RB：用于上层 read */
    uint32_t rx_last_pos; /* 上次处理的 DMA 写指针 */

    volatile uint8_t tx_busy;  // 0/1
    uint32_t last_tx_len;

    bool isCompatible;
    hal_uart_evt_cb_t cb; /* 事件回调函数 */
};

static struct hal_uart g_uarts[HAL_UART_ID_MAX];

/* =========================
 * 系列差异：H7/F7 Cache（默认空）
 * 可在 series/h7 覆盖实现
 * ========================= */
__WEAK void stm32_uart_dma_tx_clean(const void *ptr, uint32_t len) {
    (void)ptr;
    (void)len;
}
__WEAK void stm32_uart_dma_rx_invalidate(const void *ptr, uint32_t len) {
    (void)ptr;
    (void)len;
}

/**
 * @brief 判断当前数字是否是 2的幂次大小
 * @param size 传入的大小
 * @return
 */
static inline bool isPowerOfTwo_Size(uint32_t size) {
    return (size != 0) && ((size & (size - 1)) == 0);
}
/**
 * @brief 调用注册的回调函数
 * @param u 串口句柄
 * @param evt 发生的事件类型 以及对应的参数 比如增量、或者错误原因
 */
static inline void emit_evt(const hal_uart_t *u, const hal_uart_event_t *evt) {
    if (u->cb) u->cb(u->cb_user, evt);
}

/**
 * @beief 返回当前串口DMA接收指针的具体位置
 * @param u 串口句柄
 * @return
 * @note 返回的是下一个可以直接填写的指针偏移量
 */
static inline uint32_t dma_pos(const hal_uart_t *u) {
    /*  获取当前 DMA 传输中剩余的数据量 */
    const uint32_t ndtr = __HAL_DMA_GET_COUNTER(u->bsp.hdma_rx);
    /* (DMA传输长度 - 当前待传输的数据量) %  DMA传输长度  计算出 当前所在的地址*/
    // (1024（0-1023） - 512 ) %　1024 = 512
    const uint32_t pos  = (u->bsp.rx_dma_len - ndtr) % u->bsp.rx_dma_len;
    return pos;
}

/**
 *
 * @param u 串口句柄
 * @note hal_uart_t 中的DMA长度必须为 2的幂次大小 软件RB大小应尽量为 2的幂次大小
 * 严格模式：
 *   - 若 RB 剩余空间 < delta：不写入任何字节，推进 rx_last_pos 丢弃，发 ERROR(overflow)
 * 兼容模式：
 *   - 写能写的，剩余丢弃，推进 rx_last_pos，发 ERROR(overflow 可选)
 */
static void rx_commit_delta(hal_uart_t *u) {
    /* DMA接收总长度 */
    const uint32_t len  = u->bsp.rx_dma_len;
    /* DMA当前位置 */
    const uint32_t pos  = dma_pos(u);
    /* 记录的上次DMA指针地址 */
    const uint32_t last = u->rx_last_pos;
    if (pos == last) return;

    /* 计算出 当前新的位置 距离上次的位置的 长度  无论是否回环*/
    const uint32_t delta = (pos + len - last) & (len - 1);
    if (delta == 0u) return;

    /* cache invalidate：H7/F7 可覆盖 */
    stm32_uart_dma_rx_invalidate(u->bsp.rx_dma_buf, u->bsp.rx_dma_len);

    /* 先看空间 */
    const uint32_t remain = RingBuffer_GetRemainSizeFromISR(&u->rb);

    /* 严格模式 */
    if (!u->isCompatible) {
        /* 严格：空间不足 -> 全丢 */
        if (remain < delta) {
            u->rx_last_pos     = pos;                               /* 更新上次坐标为新的坐标 */
            hal_uart_event_t e = {.type = HAL_UART_EVT_ERROR};      /* 返回事件类型 错误 + 原因 */
            e.err.flags        = (uint32_t)RET_ERRNO_DATA_OVERFLOW; /* 错误类型 ：数据溢出*/
            emit_evt(u, &e);                                        /* 执行事件回调函数 */
            return;
        }
        /* 空间足够，完整写入 */
        /* 分段写入 判断是否回环*/
        const uint32_t first = (last + delta <= len) ? delta : (len - last);

        uint32_t w           = first;
        (void)WriteRingBufferFromISR(&u->rb, &u->bsp.rx_dma_buf[last], &w, 0);

        /* 回环了写入第二段 */
        if (first < delta) {
            const uint32_t second = delta - first;
            uint32_t w2           = second;
            (void)WriteRingBufferFromISR(&u->rb, &u->bsp.rx_dma_buf[0], &w2,
                                         /*isForceWrite=*/0);
        }
        u->rx_last_pos       = pos;                       /* 更新上次的位置 */
        hal_uart_event_t evt = {.type = HAL_UART_EVT_RX}; /* 返回事件类型 有接收到新的数据 */
        evt.rx.bytes         = delta;                     /* 增量长度 */
        emit_evt(u, &evt);                                /* 执行事件回调函数 */
        return;
    }

    /* 兼容模式: 尽力写，丢弃多余 */
    /* 取容量 和 需要写入的大小  中的较小值 */
    const uint32_t to_write = (remain < delta) ? remain : delta;
    /* 计算丢弃了多少字节 */
    const uint32_t dropped  = delta - to_write;

    if (to_write > 0u) {
        /* 计算没有回环的第一段 大小 */
        const uint32_t first_need = (last + to_write <= len) ? to_write : (len - last);

        uint32_t w                = first_need;
        (void)WriteRingBufferFromISR(&u->rb, &u->bsp.rx_dma_buf[last], &w, 1);
        /* 判断是否回环 回环就写余下的部分 */
        if (first_need < to_write) {
            const uint32_t second_need = to_write - first_need;
            uint32_t w2                = second_need;
            (void)WriteRingBufferFromISR(&u->rb, &u->bsp.rx_dma_buf[0], &w2, 1);
        }

        hal_uart_event_t evt = {.type = HAL_UART_EVT_RX}; /* 返回事件类型 有接收到新的数据 */
        evt.rx.bytes         = to_write;                  /* 增量长度 */
        emit_evt(u, &evt);                                /* 执行事件回调函数 */
    }

    u->rx_last_pos = pos; /* 更新上次的位置 */

    /* 当丢弃了数据 返回错误 */
    if (dropped > 0u) {
        hal_uart_event_t e = {.type = HAL_UART_EVT_ERROR};      /* 返回事件类型 错误 + 原因 */
        e.err.flags        = (uint32_t)RET_ERRNO_DATA_OVERFLOW; /* 错误类型 ：数据溢出*/
        emit_evt(u, &e);                                        /* 执行事件回调函数 */
    }
}

/**
 * @brief 放在 HAL_UART_TxCpltCallback 中利用事件回调通知上层
 * @param huart 串口句柄
 */
void hal_uart_txCp_case(const UART_HandleTypeDef *huart) {
    for (int i = 0; i < (int)HAL_UART_ID_MAX; i++) {
        hal_uart_t *u = &g_uarts[i];
        if (u->bsp.huart == huart) {
            u->tx_busy           = 0;
            hal_uart_event_t evt = {.type = HAL_UART_EVT_TX_DONE};
            evt.tx.bytes         = u->last_tx_len;
            emit_evt(u, &evt);
            return;
        }
    }
}

/**
 * @brief 放在 HAL_UART_ErrorCallback 中利用事件回调通知上层
 * @param huart 串口句柄
 * @note  evt.err.flags 上层需要自己映射
 */
void hal_uart_error_case(const UART_HandleTypeDef *huart) {
    for (int i = 0; i < (int)HAL_UART_ID_MAX; i++) {
        const hal_uart_t *u = &g_uarts[i];
        if (u->bsp.huart == huart) {
            hal_uart_event_t evt = {.type = HAL_UART_EVT_ERROR};
            evt.err.flags        = (uint32_t)huart->ErrorCode;
            emit_evt(u, &evt);
            return;
        }
    }
}

/**
 * @brief 放置到 HAL_UARTEx_RxEventCallback RB 提交当前增量
 * @param huart 串口句柄
 * @param Size  接收到的大小
 * @note 注意 需要判断当前芯片是否有空闲中断后 定义 HAL_UARTEx_ReceiveToIdle_DMA 宏
 */
#if defined(USE_HAL_UARTEx_ReceiveToIdle_DMA)
void hal_uart_rx_event_case(const UART_HandleTypeDef *huart, uint16_t Size) {
    (void)Size;
    for (int i = 0; i < (int)HAL_UART_ID_MAX; i++) {
        hal_uart_t *u = &g_uarts[i];
        if (u->bsp.huart == huart) {
            rx_commit_delta(u);
            return;
        }
    }
}
#endif

/**
 * @brief  消除空闲中断的flag
 * @param u 抽象串口句柄
 */
static void handle_idle_irq(hal_uart_t *u) {
#if defined(UART_FLAG_IDLE)
    if (__HAL_UART_GET_FLAG(u->bsp.huart, UART_FLAG_IDLE) != RESET) {
        __HAL_UART_CLEAR_IDLEFLAG(u->bsp.huart);
        rx_commit_delta(u);
    }
#endif
}

/* =========================
 * IRQ 入口：在 stm32xx_it.c 的 USARTx_IRQHandler 调用
 * ========================= */
void stm32_uart_irq_usart(hal_uart_id_t id) {
    if (id >= HAL_UART_ID_MAX) return;
    hal_uart_t *u = &g_uarts[id];
    if (!u->bsp.huart) return;

    HAL_UART_IRQHandler(u->bsp.huart);
    handle_idle_irq(u);
}

void stm32_uart_irq_dma_rx(hal_uart_id_t id) {
    if (id >= HAL_UART_ID_MAX) return;
    hal_uart_t *u = &g_uarts[id];
    if (u->bsp.hdma_rx) HAL_DMA_IRQHandler(u->bsp.hdma_rx);
}

void stm32_uart_irq_dma_tx(hal_uart_id_t id) {
    if (id >= HAL_UART_ID_MAX) return;
    const hal_uart_t *u = &g_uarts[id];
    if (u->bsp.hdma_tx) HAL_DMA_IRQHandler(u->bsp.hdma_tx);
}

/**
 * @brief 将bsp实现  串口参数 填入本地参数然后通过 hal_uart_t **out 返回
 * @param id 串口id
 * @param cfg 串口配置
 * @param out 返回配置好的串口句柄
 * @return 返回状态码
 */
ret_code_t hal_uart_port_open(hal_uart_id_t id, const hal_uart_cfg_t *cfg, hal_uart_t **out) {
    /* 参数错误检查 */
    if (!out) return UART_RET(RET_ERRNO_INVALID_ARG);
    if (id >= HAL_UART_ID_MAX) return UART_RET(RET_ERRNO_INVALID_ARG);

    /* 获取到该id 对应的静态数组地址 */
    hal_uart_t *u = &g_uarts[id];
    /* 初始填充 0 */
    memset(u, 0, sizeof(*u));
    /* 填充id */
    u->id         = id;
    /* 将地址传输过去 填充实现的bsp 参数 */
    ret_code_t rc = stm32_uart_bsp_get(id,  &u->bsp);
    if (ret_is_err(rc)) return rc;

    /* 参数检查传输的 指针是否有效, DMA长度是否是2的幂次大小 */
    if (!u->bsp.huart || !u->bsp.hdma_rx || !u->bsp.hdma_tx || !u->bsp.rx_dma_buf ||
        u->bsp.rx_dma_len < 2u || !isPowerOfTwo_Size(u->bsp.rx_dma_len) || u->bsp.sw_rb_len < 2u) {
        return UART_RET(RET_ERRNO_INVALID_ARG);
    }

    /* 兼容模式（cfg 没有就默认 false=严格） */
    u->isCompatible = (cfg ? cfg->isCompatible : false);
    char name[32]   = {0};
    sprintf(name, "stm32_port_uart_RB%d", id);
    rc = CreateRingBuffer(&u->rb, name, u->bsp.sw_rb_len);
    if (ret_is_err(rc)) return rc;

    /* 串口参数配置 */
    if (cfg) {
        u->bsp.huart->Init.BaudRate = cfg->baud;
        u->bsp.huart->Init.HwFlowCtl =
            cfg->flow_ctrl ? UART_HWCONTROL_RTS_CTS : UART_HWCONTROL_NONE;
        u->bsp.huart->Init.Parity = cfg->parity;
        u->bsp.huart->Init.StopBits =
            cfg->stop_bits == STOPBITS_1 ? UART_STOPBITS_1 : UART_STOPBITS_2;
        u->bsp.huart->Init.WordLength =
            cfg->data_bits == WORDLENGTH_8B ? UART_WORDLENGTH_8B : UART_WORDLENGTH_9B;
    }

    /* 串口初始化 */
    if (HAL_UART_Init(u->bsp.huart) != HAL_OK) return UART_RET(RET_ERRNO_IO);

    /* NVIC 可由 BSP 配，也可这里开 */
    if (u->bsp.usart_irq > 0) {
        HAL_NVIC_SetPriority(u->bsp.usart_irq, u->bsp.irq_prio, 0);
        HAL_NVIC_EnableIRQ(u->bsp.usart_irq);
    }
    if (u->bsp.dma_rx_irq > 0) {
        HAL_NVIC_SetPriority(u->bsp.dma_rx_irq, u->bsp.irq_prio, 0);
        HAL_NVIC_EnableIRQ(u->bsp.dma_rx_irq);
    }
    if (u->bsp.dma_tx_irq > 0) {
        HAL_NVIC_SetPriority(u->bsp.dma_tx_irq, u->bsp.irq_prio, 0);
        HAL_NVIC_EnableIRQ(u->bsp.dma_tx_irq);
    }

    u->rx_last_pos = 0;
    u->tx_busy     = 0;
    u->last_tx_len = 0;

    *out           = (hal_uart_t *)u;
    return RET_OK;
}

/**
 * @brief 将串口恢复默认配置
 * @param h 串口句柄
 */
ret_code_t hal_uart_port_close(hal_uart_t *h) {
    if (!h) return UART_RET(RET_ERRNO_INVALID_ARG);
    const hal_uart_t *u = (hal_uart_t *)h;
    if (HAL_UART_DeInit(u->bsp.huart) != HAL_OK) return UART_RET(RET_ERRNO_IO);
    return RET_OK;
}

/**
 * @brief 注册串口事件回调函数 和 上下文
 * @param h 串口句柄
 * @param cb 事件回调函数
 * @param user user上下文
 */
ret_code_t hal_uart_port_set_evt_cb(hal_uart_t *h, hal_uart_evt_cb_t cb, void *user) {
    if (!h) return UART_RET(RET_ERRNO_INVALID_ARG);
    hal_uart_t *u = (hal_uart_t *)h;
    u->cb         = cb;
    u->cb_user    = user;
    return RET_OK;
}

/**
 * @brief 根据宏定义选择不同的串口数据接收方式
 * @param h 串口句柄
 * @return 状态码
 */
ret_code_t hal_uart_port_rx_start(hal_uart_t *h) {
    if (!h) return UART_RET(RET_ERRNO_INVALID_ARG);
    hal_uart_t *u = (hal_uart_t *)h;

#if defined(USE_HAL_UARTEx_ReceiveToIdle_DMA)
    /* DMA + IDLE 方式接收方式 */
    if (HAL_UARTEx_ReceiveToIdle_DMA(u->bsp.huart, u->bsp.rx_dma_buf,
                                     (uint16_t)u->bsp.rx_dma_len) != HAL_OK)
        return UART_RET(RET_ERRNO_IO);
    /* 可选：关 HT 降低中断 */
#if (DISABLE_DMA_IT_HT)
    __HAL_DMA_DISABLE_IT(u->bsp.huart->hdmarx, DMA_IT_HT);
#endif

#else
    /* 普通 一次性DMA传输 */
    if (HAL_UART_Receive_DMA(u->bsp.huart, u->bsp.rx_dma_buf, (uint16_t)u->bsp.rx_dma_len) !=
        HAL_OK)
        return UART_RET(RET_ERRNO_IO);
    /* DMA 接收开启失败 退化为中断接收 */
#if defined(UART_IT_IDLE)
    /* 中断方式的传输 */
    __HAL_UART_ENABLE_IT(u->bsp.huart, UART_IT_IDLE);
#endif
#endif
    u->rx_last_pos = dma_pos(u);
    return RET_OK;
}

/**
 * @brief 将=数据通过串口进行异步发送
 * @param h 串口句柄
 * @param buf 将要发送的数据地址
 * @param len 数据长度
 * @return 状态码
 */
ret_code_t hal_uart_port_send_async(hal_uart_t *h, const uint8_t *buf, uint32_t len) {
    if (!h || !buf || len == 0u) return UART_RET(RET_ERRNO_INVALID_ARG);
    hal_uart_t *u = (hal_uart_t *)h;

    if (u->tx_busy) return UART_RET(RET_ERRNO_BUSY);
    u->tx_busy     = 1;
    u->last_tx_len = len;

    /** H7/F7 缓存处理 */
    stm32_uart_dma_tx_clean(buf, len);

    if (HAL_UART_Transmit_DMA(u->bsp.huart, (uint8_t *)buf, (uint16_t)len) != HAL_OK) {
        /* 状态回滚 */
        u->tx_busy = 0;
        return UART_RET(RET_ERRNO_IO);
    }
    return RET_OK;
}

/* 读：严格=必须足够才读且不消费；兼容=尽力读 */
/**
 *
 * @param h 串口句柄
 * @param out 数据接收地址
 * @param want 想要读取的字节数
 * @param nread 实际读取的字节数
 * @return
 */
ret_code_t hal_uart_port_read(hal_uart_t *h, uint8_t *out, uint32_t want, uint32_t *nread) {
    if (!h || !out || want == 0u || !nread) return UART_RET(RET_ERRNO_INVALID_ARG);
    hal_uart_t *u       = (hal_uart_t *)h;

    uint32_t size       = want;
    const ret_code_t rc = ReadRingBuffer(&u->rb, out, &size, u->isCompatible ? 1 : 0);

    /* 约定：严格模式下若不足，ReadRingBuffer 应返回 DATA_NOT_ENOUGH 且 size不变；
       兼容模式下 size 为实际读取量。 */
    *nread              = size;
    return rc;
}
