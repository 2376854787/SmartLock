#include "APP_config.h"
#include "stm32_hal_config.h"
/* hal抽象选择宏 */
#if (defined(CFG_TARGET_PLATFORM_STM32_HAL) && (CFG_TARGET_PLATFORM_STM32_HAL == 1)) && \
    (defined(CFG_FEAT_HAL_UART) && (CFG_FEAT_HAL_UART == 1))
#include <stdio.h>

#include "RingBuffer.h"
#include "hal_uart.h"
#include "osal.h"
#include "ret_code.h"
#include "stm32_hal.h"
#include "stm32_uart_bsp.h"
#include "stm32_uart_series.h"

/* 根据当前所属模块id 与返回状态生成32位状态码 */
#define UART_RET(clas_, errno_) \
    RET_MAKE(RET_MOD_PORT, RET_SUB_PORT_STM32, RET_CODE_MAKE((clas_), (errno_)))

/* ========== UART 上下文 ========== */
struct hal_uart {
    const char* name;         /* 串口名称*/
    void* cb_user;            /* 用户上下文 */
    hal_uart_id_t id;         /* 串口内部id*/
    stm32_uart_bsp_t bsp;     /* 串口映射配置 */
    char rb_name[32];         /* 软件 RB 名称 */
    RingBuffer rb;            /* 软件 RB：用于上层 read */
    bool rb_ready;            /* 软件 RB 是否已初始化 */
    bool opened;              /* 端口是否已打开 */
    uint32_t rx_last_pos;     /* 上次处理的 DMA 写指针 */
    volatile uint8_t tx_busy; /* 0/1 */
    uint32_t last_tx_len;     /* 上次DMA的位置 */
    bool isCompatible;        /* RB 严格模式/ 兼容模式 */
    hal_uart_evt_cb_t cb;     /* 事件回调函数 */
};
/*　存储全局串口资源 */
static struct hal_uart g_uarts[HAL_UART_ID_MAX];
ret_code_t hal_uart_port_read_reserve(hal_uart_t* h, uint32_t want, hal_uart_read_span_t* out,
                                      uint32_t* nread);
ret_code_t hal_uart_port_read_commit(hal_uart_t* h, uint32_t nread);
/**
 * @brief 占位
 * @param ptr DMA初始地址
 * @param len 长度
 * @note 在使用 F7/H7 必须覆盖实现并 32位字节对齐
 */
__WEAK void stm32_uart_dma_tx_clean(const void* ptr, uint32_t len) {
    (void)ptr;
    (void)len;
}
/**
 * @brief 占位
 * @param ptr DMA初始地址
 * @param len 长度
 * @note 在使用 F7/H7 必须覆盖实现并 32位字节对齐
 */
__WEAK void stm32_uart_dma_rx_invalidate(const void* ptr, uint32_t len) {
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
static inline void emit_evt(const hal_uart_t* u, const hal_uart_event_t* evt) {
    if (u->cb) u->cb(u->cb_user, evt);
}
/**
 * @brief 装填错误事件以及flags
 * @param u 串口句柄
 * @param flags 传递的消息 接收到的字节数、
 */
static inline void emit_err_evt(const hal_uart_t* u, uint32_t flags) {
    hal_uart_event_t evt = {.type = HAL_UART_EVT_ERROR};
    evt.err.flags        = flags;
    emit_evt(u, &evt);
}
/**
 * @brief 将stm32的hal错误转换为自定义的错误类型
 * @param hal_err stm32hal返回的错误
 * @return
 */
static ret_code_t uart_map_hal_error(uint32_t hal_err) {
#if defined(HAL_UART_ERROR_ORE)
    if ((hal_err & HAL_UART_ERROR_ORE) != 0u) {
        return UART_RET(RET_CLASS_DATA, RET_R_DATA_OVERFLOW);
    }
#endif
    (void)hal_err;
    return UART_RET(RET_CLASS_IO, RET_R_IO);
}
/**
 * @brief 获取接收的长度 并 清除busy 标志位（临界区保护）、将DMA上次接收的索引改为0
 * @param u 串口句柄
 * @return 接收到的长度
 */
static inline uint32_t uart_take_tx_len_and_clear_busy(hal_uart_t* u) {
    osal_crit_state_t cs = 0u;
    uint32_t tx_len      = 0u;
    OSAL_enter_critical_ex(&cs);
    tx_len         = u->last_tx_len;
    u->last_tx_len = 0u;
    u->tx_busy     = 0u;
    OSAL_exit_critical_ex(cs);
    return tx_len;
}
/**
 * @beief 返回当前串口DMA接收指针的具体位置
 * @param u 串口句柄
 * @return
 * @note 返回的是下一个可以直接填写的指针偏移量
 */
static inline uint32_t dma_pos(const hal_uart_t* u) {
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
static void rx_commit_delta(hal_uart_t* u) {
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

    /* 获取可以写的空间大小 */
    RingBufferSpan span = {0};
    uint32_t granted    = 0u;
    const ret_code_t rc =
        RingBuffer_WriteReserve_SPSC(&u->rb, delta, &span, &granted, u->isCompatible);

    /* 严格模式：空间不足全丢 */
    if (!u->isCompatible && ret_is_err(rc)) {
        u->rx_last_pos = pos;
        emit_err_evt(u, UART_RET(RET_CLASS_RESOURCE, RET_R_BUFFER_FULL));
        return;
    }

    const uint32_t to_write = granted;
    const uint32_t dropped  = delta - to_write;

    if (to_write > 0u) {
        /* 从DMA 搬运到 uart 的rb */
        RingBuffer_SpanWriteFromCircular(&span, u->bsp.rx_dma_buf, len, last, to_write);
        /* 提交搬运的信息更新 容器索引 */
        const ret_code_t commit_rc = RingBuffer_WriteCommit_SPSC(&u->rb, to_write);
        if (ret_is_ok(commit_rc)) {
            hal_uart_event_t evt = {.type = HAL_UART_EVT_RX};
            evt.rx.bytes         = to_write;
            emit_evt(u, &evt);
        } else {
            emit_err_evt(u, UART_RET(RET_CLASS_RESOURCE, RET_R_BUFFER_FULL));
        }
    }
    u->rx_last_pos = pos;
    if (dropped > 0u) {
        emit_err_evt(u, UART_RET(RET_CLASS_RESOURCE, RET_R_BUFFER_FULL));
    }
}

/**
 * @brief 放在 HAL_UART_TxCpltCallback 中利用事件回调通知上层
 * @param huart 串口句柄
 */
void hal_uart_txCp_case(const UART_HandleTypeDef* huart) {
    for (int i = 0; i < (int)HAL_UART_ID_MAX; i++) {
        hal_uart_t* u = &g_uarts[i];
        if (u->opened && u->bsp.huart == huart) {
            hal_uart_event_t evt = {.type = HAL_UART_EVT_TX_DONE};
            /* 接收完成 获取长度清理busy 以及dma 位置更新为 0*/
            evt.tx.bytes         = uart_take_tx_len_and_clear_busy(u);
            emit_evt(u, &evt);
            return;
        }
    }
}

/**
 * @brief 放在 HAL_UART_ErrorCallback 中利用事件回调通知上层
 * @param huart 串口句柄
 * @note  evt.err.flags 统一返回 ret_code 语义（非裸 HAL ErrorCode）
 */
void hal_uart_error_case(const UART_HandleTypeDef* huart) {
    for (int i = 0; i < (int)HAL_UART_ID_MAX; i++) {
        hal_uart_t* u = &g_uarts[i];
        if (u->opened && u->bsp.huart == huart) {
            (void)uart_take_tx_len_and_clear_busy(u);
            emit_err_evt(u, uart_map_hal_error((uint32_t)huart->ErrorCode));
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
#if (defined(CFG_PARAM_UART_RX_USE_DMA_IDLE) && (CFG_PARAM_UART_RX_USE_DMA_IDLE == 1))
void hal_uart_rx_event_case(const UART_HandleTypeDef* huart, uint16_t Size) {
    (void)Size;
    for (int i = 0; i < (int)HAL_UART_ID_MAX; i++) {
        hal_uart_t* u = &g_uarts[i];
        if (u->opened && u->bsp.huart == huart) {
            rx_commit_delta(u);
            return;
        }
    }
}
#endif
/**
 * @brief 放置于 DMA中断 用于 半满全满触发搬运 + 递增 +通知上层
 * @param huart 串口句柄
 */
void hal_uart_rx_dma_progress_case(const UART_HandleTypeDef* huart) {
    for (int i = 0; i < (int)HAL_UART_ID_MAX; i++) {
        hal_uart_t* u = &g_uarts[i];
        if (u->opened && u->bsp.huart == huart) {
            rx_commit_delta(u);
            return;
        }
    }
}

/**
 * @brief  消除空闲中断的flag 提交DMA接收增量
 * @param u 抽象串口句柄
 */
static void handle_idle_irq(hal_uart_t* u) {
#if defined(UART_FLAG_IDLE)
    if (__HAL_UART_GET_FLAG(u->bsp.huart, UART_FLAG_IDLE) != RESET) {
        __HAL_UART_CLEAR_IDLEFLAG(u->bsp.huart);
        rx_commit_delta(u);
    }
#endif
}

/**
 * @brief 执行 HAL_UART_IRQHandler + 检测DMA接收增量并提交
 * @param id 串口句柄
 */
void stm32_uart_irq_usart(hal_uart_id_t id) {
    if (id >= HAL_UART_ID_MAX) return;
    hal_uart_t* u = &g_uarts[id];
    if (!u->opened || !u->bsp.huart) return;

    HAL_UART_IRQHandler(u->bsp.huart);
    handle_idle_irq(u);
}

/**
 * @brief 执行 HAL_DMA_IRQHandler
 * @param id 串口句柄
 */
void stm32_uart_irq_dma_rx(hal_uart_id_t id) {
    if (id >= HAL_UART_ID_MAX) return;
    const hal_uart_t* u = &g_uarts[id];
    if (!u->opened) return;
    if (u->bsp.hdma_rx) HAL_DMA_IRQHandler(u->bsp.hdma_rx);
}

/**
 * @brief 执行 HAL_DMA_IRQHandler
 * @param id 串口句柄
 */
void stm32_uart_irq_dma_tx(hal_uart_id_t id) {
    if (id >= HAL_UART_ID_MAX) return;
    const hal_uart_t* u = &g_uarts[id];
    if (!u->opened) return;
    if (u->bsp.hdma_tx) HAL_DMA_IRQHandler(u->bsp.hdma_tx);
}

/**
 * @brief 将bsp实现  串口参数 填入本地参数然后通过 hal_uart_t **out 返回
 * @param id 串口id
 * @param cfg 串口配置
 * @param out 返回配置好的串口句柄
 * @return 返回状态码
 */
ret_code_t hal_uart_port_open(hal_uart_id_t id, const hal_uart_cfg_t* cfg, hal_uart_t** out) {
    /* 参数错误检查 */
    if (!cfg || !out) return UART_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    if (id >= HAL_UART_ID_MAX) return UART_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);

    /* 获取到该id 对应的静态数组地址 */
    hal_uart_t* u = &g_uarts[id];
    if (u->opened) return UART_RET(RET_CLASS_STATE, RET_R_BUSY);
    u->id                = id;

    stm32_uart_bsp_t bsp = {0};
    /* 将静态池成员对应地址传输过去 填充实现的bsp 参数 */
    ret_code_t rc        = stm32_uart_bsp_get(id, &bsp);
    if (ret_is_err(rc)) return rc;

    /* 参数检查传输的 指针是否有效, DMA长度是否是2的幂次大小 */
    if (!bsp.huart || !bsp.hdma_rx || !bsp.hdma_tx || !bsp.rx_dma_buf || bsp.rx_dma_len < 2u ||
        !isPowerOfTwo_Size(bsp.rx_dma_len) || bsp.sw_rb_len < 2u) {
        return UART_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    }

    if (!u->rb_ready) {
        (void)snprintf(u->rb_name, sizeof(u->rb_name), "stm32_port_uart_RB%d", id);
        rc = CreateRingBuffer(&u->rb, u->rb_name, bsp.sw_rb_len);
        if (ret_is_err(rc)) return rc;
        u->rb_ready = true;
    } else {
        if (u->rb.size != bsp.sw_rb_len) {
            return UART_RET(RET_CLASS_STATE, RET_R_STATE_ERR);
        }
        rc = ResetRingBuffer(&u->rb);
        if (ret_is_err(rc)) return rc;
    }

    u->bsp                       = bsp;
    u->isCompatible              = cfg->isCompatible;
    u->cb                        = NULL;
    u->cb_user                   = NULL;
    u->tx_busy                   = 0u;
    u->last_tx_len               = 0u;
    u->rx_last_pos               = 0u;
    u->opened                    = false;

    /* 串口参数配置 */
    u->bsp.huart->Init.BaudRate  = cfg->baud;
    u->bsp.huart->Init.HwFlowCtl = cfg->flow_ctrl ? UART_HWCONTROL_RTS_CTS : UART_HWCONTROL_NONE;
    u->bsp.huart->Init.Parity    = cfg->parity;
    u->bsp.huart->Init.StopBits  = cfg->stop_bits == STOPBITS_1 ? UART_STOPBITS_1 : UART_STOPBITS_2;
    u->bsp.huart->Init.WordLength =
        cfg->data_bits == WORDLENGTH_8B ? UART_WORDLENGTH_8B : UART_WORDLENGTH_9B;

    /* 串口初始化 */
    if (HAL_UART_Init(u->bsp.huart) != HAL_OK) return UART_RET(RET_CLASS_IO, RET_R_IO);

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

    u->rx_last_pos = 0u;
    u->tx_busy     = 0u;
    u->last_tx_len = 0u;
    u->opened      = true;

    *out           = (hal_uart_t*)u;
    return RET_OK;
}

/**
 * @brief 将串口恢复默认配置
 * @param h 串口句柄
 */
ret_code_t hal_uart_port_close(hal_uart_t* h) {
    if (!h) return UART_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    hal_uart_t* u = (hal_uart_t*)h;
    if (!u->opened) return UART_RET(RET_CLASS_STATE, RET_R_NOT_READY);

    (void)HAL_UART_DMAStop(u->bsp.huart);
    if (HAL_UART_DeInit(u->bsp.huart) != HAL_OK) return UART_RET(RET_CLASS_IO, RET_R_IO);

    u->opened      = false;
    u->cb          = NULL;
    u->cb_user     = NULL;
    u->tx_busy     = 0u;
    u->last_tx_len = 0u;
    u->rx_last_pos = 0u;
    if (u->rb_ready) {
        (void)ResetRingBuffer(&u->rb);
    }
    return RET_OK;
}

/**
 * @brief 注册串口事件回调函数 和 上下文
 * @param h 串口句柄
 * @param cb 事件回调函数
 * @param user user上下文
 */
ret_code_t hal_uart_port_set_evt_cb(hal_uart_t* h, hal_uart_evt_cb_t cb, void* user) {
    if (!h) return UART_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    hal_uart_t* u = (hal_uart_t*)h;
    if (!u->opened) return UART_RET(RET_CLASS_STATE, RET_R_NOT_READY);
    u->cb      = cb;
    u->cb_user = user;
    return RET_OK;
}

/**
 * @brief 根据宏定义选择不同的串口数据接收方式
 * @param h 串口句柄
 * @return 状态码
 */
ret_code_t hal_uart_port_rx_start(hal_uart_t* h) {
    if (!h) return UART_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    hal_uart_t* u = (hal_uart_t*)h;
    if (!u->opened) return UART_RET(RET_CLASS_STATE, RET_R_NOT_READY);

#if (defined(CFG_PARAM_UART_RX_USE_DMA_IDLE) && (CFG_PARAM_UART_RX_USE_DMA_IDLE == 1))
    /* DMA + IDLE 方式接收方式 */
    if (HAL_UARTEx_ReceiveToIdle_DMA(u->bsp.huart, u->bsp.rx_dma_buf,
                                     (uint16_t)u->bsp.rx_dma_len) != HAL_OK)
        return UART_RET(RET_CLASS_IO, RET_R_IO);
    /* 可选：关 HT 降低中断 */
#if (defined(CFG_PARAM_UART_DISABLE_DMA_IT_HT) && (CFG_PARAM_UART_DISABLE_DMA_IT_HT == 1))
    __HAL_DMA_DISABLE_IT(u->bsp.huart->hdmarx, DMA_IT_HT);
#endif

#else
    /* 普通 一次性DMA传输 */
    if (HAL_UART_Receive_DMA(u->bsp.huart, u->bsp.rx_dma_buf, (uint16_t)u->bsp.rx_dma_len) !=
        HAL_OK)
        return UART_RET(RET_CLASS_IO, RET_R_IO);
    /* DMA 接收开启成功 */
#if defined(UART_IT_IDLE)
    /* 使能空闲中断 */
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
ret_code_t hal_uart_port_send_async(hal_uart_t* h, const uint8_t* buf, uint32_t len) {
    if (!h || !buf || len == 0u) return UART_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    hal_uart_t* u = (hal_uart_t*)h;
    if (!u->opened) return UART_RET(RET_CLASS_STATE, RET_R_NOT_READY);
    if (len > (uint32_t)UINT16_MAX) return UART_RET(RET_CLASS_PARAM, RET_R_RANGE_ERR);

    osal_crit_state_t cs = 0u;
    OSAL_enter_critical_ex(&cs);
    if (u->tx_busy) {
        OSAL_exit_critical_ex(cs);
        return UART_RET(RET_CLASS_STATE, RET_R_BUSY);
    }
    u->tx_busy     = 1u;
    u->last_tx_len = len;
    OSAL_exit_critical_ex(cs);

    /** H7/F7 缓存处理 */
    stm32_uart_dma_tx_clean(buf, len);

    if (HAL_UART_Transmit_DMA(u->bsp.huart, (uint8_t*)buf, (uint16_t)len) != HAL_OK) {
        /* 状态回滚 */
        (void)uart_take_tx_len_and_clear_busy(u);
        return UART_RET(RET_CLASS_IO, RET_R_IO);
    }
    return RET_OK;
}

/**
 *
 * @param h 串口句柄
 * @param out 数据接收地址
 * @param want 想要读取的字节数
 * @param nread 实际读取的字节数
 * @return状态码
 * @note 读：严格=必须足够才读且不消费；兼容=尽力读
 */
ret_code_t hal_uart_port_read(hal_uart_t* h, uint8_t* out, uint32_t want, uint32_t* nread) {
    if (!h || !out || want == 0u || !nread) return UART_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    hal_uart_t* u = (hal_uart_t*)h;
    if (!u->opened) return UART_RET(RET_CLASS_STATE, RET_R_NOT_READY);
    hal_uart_read_span_t span = {0};
    uint32_t granted          = 0u;
    ret_code_t rc             = hal_uart_port_read_reserve(u, want, &span, &granted);
    if (ret_is_err(rc)) return rc;

    RingBuffer_SpanReadToLinear(&span, out, granted);
    rc     = hal_uart_port_read_commit(u, granted);
    *nread = granted;
    return rc;
}
/**
 * @brief 申请串口接收缓冲区中的可读窗口
 * @param h 串口句柄
 * @param want 想要读取的字节数
 * @param out 数据接收地址
 * @param nread 实际读取的字节数
 * @return状态码
 * @note 读：严格=必须足够才读且不消费；兼容=尽力读
 */
ret_code_t hal_uart_port_read_reserve(hal_uart_t* h, uint32_t want, hal_uart_read_span_t* out,
                                      uint32_t* nread) {
    if (nread) *nread = 0u;
    if (!h || !out || !nread) return UART_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    hal_uart_t* u = (hal_uart_t*)h;
    if (!u->opened) return UART_RET(RET_CLASS_STATE, RET_R_NOT_READY);

    uint32_t want_local = want;
    if (want_local == 0u) {
        want_local =
            OSAL_in_isr() ? RingBuffer_GetUsedSizeFromISR(&u->rb) : RingBuffer_GetUsedSize(&u->rb);
        if (want_local == 0u) {
            out->p1 = NULL;
            out->p2 = NULL;
            out->n1 = 0u;
            out->n2 = 0u;
            *nread  = 0u;
            return RET_OK;
        }
    }

    RingBufferSpan span = {0};
    uint32_t granted    = 0u;
    const ret_code_t rc =
        RingBuffer_ReadReserve_SPSC(&u->rb, want_local, &span, &granted, u->isCompatible);
    if (ret_is_err(rc)) return rc;

    out->p1 = span.p1;
    out->p2 = span.p2;
    out->n1 = span.n1;
    out->n2 = span.n2;
    *nread  = granted;
    return RET_OK;
}
/**
 * @brief 提交已经消费的接收字节数
 * @param h 句柄
 * @param nread 提交字节数
 * @return
 */
ret_code_t hal_uart_port_read_commit(hal_uart_t* h, uint32_t nread) {
    if (!h) return UART_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    if (nread == 0u) return RET_OK;

    hal_uart_t* u = (hal_uart_t*)h;
    if (!u->opened) return UART_RET(RET_CLASS_STATE, RET_R_NOT_READY);
    return RingBuffer_ReadCommit_SPSC(&u->rb, nread);
}

hal_uart_id_t hal_uart_port_get_id(const hal_uart_t* h) {
    if (!h) return HAL_UART_ID_MAX;
    return h->id;
}

#endif
