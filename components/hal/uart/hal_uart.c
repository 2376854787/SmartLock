#include "hal_uart.h"

#include <stddef.h>
#include <stdio.h>

#include "APP_config.h"
#include "RingBuffer.h"
#include "assert_cus.h"
#include "hal_uart_port.h"
#include "osal.h"

#if defined(CFG_FEAT_LOG_SYSTEM) && (CFG_FEAT_LOG_SYSTEM == 1)
#include "log.h"
#endif

#define UART_HAL_PARAM(reason_)   RET_MAKE_PARAM(RET_MOD_HAL, RET_SUB_HAL_UART, (reason_))
#define UART_HAL_STATE(reason_)   RET_MAKE_STATE(RET_MOD_HAL, RET_SUB_HAL_UART, (reason_))
#define UART_HAL_TIMEOUT(reason_) RET_MAKE_TIMEOUT(RET_MOD_HAL, RET_SUB_HAL_UART, (reason_))
#define UART_HAL_IO(reason_)      RET_MAKE_IO(RET_MOD_HAL, RET_SUB_HAL_UART, (reason_))
#define UART_HAL_RES(reason_)     RET_MAKE_RESOURCE(RET_MOD_HAL, RET_SUB_HAL_UART, (reason_))
#define UART_HAL_DATA(reason_)    RET_MAKE_DATA(RET_MOD_HAL, RET_SUB_HAL_UART, (reason_))
#define UART_HAL_FATAL(reason_)   RET_MAKE_FATAL(RET_MOD_HAL, RET_SUB_HAL_UART, (reason_))

#if (defined(CFG_FEAT_HAL_UART) && (CFG_FEAT_HAL_UART == 1))

struct hal_uart {
    hal_uart_id_t id;
    hal_uart_port_handle_t* port;
    RingBuffer rx_rb;
    char rx_rb_name[32];
    bool rb_ready;
    bool initialized;
    hal_uart_evt_cb_t evt_cb;
    void* evt_user;
};

static struct hal_uart g_uart_ctx[HAL_UART_ID_MAX];

/**
 * @brief port 层错误上报钩子（默认弱实现，可在外部重写）
 * @param rc_port port 层原始错误码
 * @param rc_hal  映射后的 HAL 错误码
 * @param api     触发错误的 HAL API 名称
 * @param arg0    辅助参数 0
 * @param arg1    辅助参数 1
 */
__attribute__((weak)) void hal_uart_on_port_error(ret_code_t rc_port, ret_code_t rc_hal,
                                                  const char* api, uint32_t arg0,
                                                  uint32_t arg1) {
#if defined(CFG_FEAT_LOG_SYSTEM) && (CFG_FEAT_LOG_SYSTEM == 1) && \
    defined(CFG_PARAM_UART_LOG_PORT_ERR) && (CFG_PARAM_UART_LOG_PORT_ERR == 1)
#if defined(CFG_PARAM_UART_LOG_PORT_ERR_IN_ISR) && (CFG_PARAM_UART_LOG_PORT_ERR_IN_ISR == 1)
    if (ret_is_err(rc_port)) {
#else
    if (ret_is_err(rc_port) && !OSAL_in_isr()) {
#endif
        printf("HAL_UART api:%s port:0x%08lX->hal:0x%08lX arg0:%lu arg1:%lu",
               (api != NULL) ? api : "unknown", (unsigned long)rc_port, (unsigned long)rc_hal,
               (unsigned long)arg0, (unsigned long)arg1);
    }
#else
    (void)rc_port;
    (void)rc_hal;
    (void)api;
    (void)arg0;
    (void)arg1;
#endif
}

/**
 * @brief 将 port 层错误码映射为 HAL UART 统一错误码
 * @param rc_port port 层返回值
 * @param api     触发映射的 HAL API 名称
 * @param arg0    辅助参数 0
 * @param arg1    辅助参数 1
 * @return 映射后的 HAL 错误码
 */
static ret_code_t uart_map_port_to_hal(ret_code_t rc_port, const char* api, uint32_t arg0,
                                       uint32_t arg1) {
    ret_code_t rc_hal = UART_HAL_IO(RET_R_IO);

    if (ret_is_ok(rc_port)) return RET_OK;

    if (ret_is_class(rc_port, RET_CLASS_PARAM)) {
        if (ret_is_reason(rc_port, RET_R_NULL_PTR))
            rc_hal = UART_HAL_PARAM(RET_R_NULL_PTR);
        else if (ret_is_reason(rc_port, RET_R_RANGE_ERR))
            rc_hal = UART_HAL_PARAM(RET_R_RANGE_ERR);
        else if (ret_is_reason(rc_port, RET_R_UNSUPPORTED))
            rc_hal = UART_HAL_PARAM(RET_R_UNSUPPORTED);
        else
            rc_hal = UART_HAL_PARAM(RET_R_INVALID_ARG);
    } else if (ret_is_class(rc_port, RET_CLASS_TIMEOUT)) {
        rc_hal = UART_HAL_TIMEOUT(RET_R_TIMEOUT);
    } else if (ret_is_class(rc_port, RET_CLASS_RESOURCE)) {
        if (ret_is_reason(rc_port, RET_R_QUEUE_FULL))
            rc_hal = UART_HAL_RES(RET_R_QUEUE_FULL);
        else if (ret_is_reason(rc_port, RET_R_BUFFER_FULL))
            rc_hal = UART_HAL_RES(RET_R_BUFFER_FULL);
        else if (ret_is_reason(rc_port, RET_R_NO_MEM))
            rc_hal = UART_HAL_RES(RET_R_NO_MEM);
        else
            rc_hal = UART_HAL_RES(RET_R_NO_RESOURCE);
    } else if (ret_is_class(rc_port, RET_CLASS_STATE)) {
        if (ret_is_reason(rc_port, RET_R_BUSY))
            rc_hal = UART_HAL_STATE(RET_R_BUSY);
        else if (ret_is_reason(rc_port, RET_R_NOT_READY))
            rc_hal = UART_HAL_STATE(RET_R_NOT_READY);
        else
            rc_hal = UART_HAL_STATE(RET_R_STATE_ERR);
    } else if (ret_is_class(rc_port, RET_CLASS_DATA)) {
        if (ret_is_reason(rc_port, RET_R_CRC))
            rc_hal = UART_HAL_DATA(RET_R_CRC);
        else if (ret_is_reason(rc_port, RET_R_DATA_NOT_ENOUGH))
            rc_hal = UART_HAL_DATA(RET_R_DATA_NOT_ENOUGH);
        else if (ret_is_reason(rc_port, RET_R_DATA_OVERFLOW))
            rc_hal = UART_HAL_DATA(RET_R_DATA_OVERFLOW);
        else
            rc_hal = UART_HAL_DATA(RET_R_PARSE_ERR);
    } else if (ret_is_class(rc_port, RET_CLASS_FATAL)) {
        rc_hal = UART_HAL_FATAL(RET_R_PANIC);
    }

    hal_uart_on_port_error(rc_port, rc_hal, api, arg0, arg1);
    return rc_hal;
}

/**
 * @brief 向已注册用户回调转发 UART 事件
 * @param h UART 句柄
 * @param evt 事件
 */
static void uart_emit_evt(const hal_uart_t* h, const hal_uart_event_t* evt) {
    if ((h != NULL) && (evt != NULL) && (h->evt_cb != NULL)) {
        h->evt_cb(h->evt_user, evt);
    }
}

/**
 * @brief 上报 HAL 语义错误事件
 * @param h UART 句柄
 * @param flags HAL 错误码
 */
static void uart_emit_error_evt(const hal_uart_t* h, uint32_t flags) {
    hal_uart_event_t evt = {.type = HAL_UART_EVT_ERROR};
    evt.err.flags        = flags;
    uart_emit_evt(h, &evt);
}

/**
 * @brief 将一段线性数据写入 HAL 接收缓冲
 * @param h UART 句柄
 * @param src 源数据
 * @param len 写入长度
 * @param written 实际写入长度
 * @return RET_OK 或 RingBuffer 错误码
 * @note HAL 始终采用尽力写入语义
 */
static ret_code_t uart_rb_write_linear(hal_uart_t* h, const uint8_t* src, uint32_t len,
                                       uint32_t* written) {
    RingBufferSpan span = {0};
    uint32_t granted    = 0u;
    ret_code_t rc       = RET_OK;

    if (written != NULL) *written = 0u;
    if ((h == NULL) || (src == NULL) || (len == 0u) || (written == NULL)) {
        return UART_HAL_PARAM(RET_R_INVALID_ARG);
    }

    /* 生产者是 CPU（SpanWriteFromLinear 由 CPU memcpy 入环），对端消费者是软件，
     * 故用 RB_SYNC_SMP。 */
    rc = RingBuffer_WriteReserve_SPSC(&h->rx_rb, len, &span, &granted, true, RB_SYNC_SMP);
    if (ret_is_err(rc)) return rc;

    if (granted > 0u) {
        RingBuffer_SpanWriteFromLinear(&span, src, granted);
        rc = RingBuffer_WriteCommit_SPSC(&h->rx_rb, granted, RB_SYNC_SMP);
        if (ret_is_err(rc)) return rc;
    }

    *written = granted;
    return RET_OK;
}

/**
 * @brief 消费 port 上报的 RX 增量并写入 HAL 接收缓冲
 * @param h UART 句柄
 * @param data port 上报的数据窗口
 */
static void uart_on_rx_ready(hal_uart_t* h, const hal_uart_port_rx_data_t* data) {
    uint32_t total_written = 0u;
    bool overflow          = false;

    if ((h == NULL) || (data == NULL)) return;

    if (data->n1 > 0u) {
        uint32_t written = 0u;
        if (ret_is_err(uart_rb_write_linear(h, data->p1, data->n1, &written))) {
            overflow = true;
        } else {
            total_written += written;
            overflow = overflow || (written != data->n1);
        }
    }

    if (data->n2 > 0u) {
        uint32_t written = 0u;
        if (ret_is_err(uart_rb_write_linear(h, data->p2, data->n2, &written))) {
            overflow = true;
        } else {
            total_written += written;
            overflow = overflow || (written != data->n2);
        }
    }

    if (total_written > 0u) {
        hal_uart_event_t evt = {.type = HAL_UART_EVT_RX};
        evt.rx.bytes         = total_written;
        uart_emit_evt(h, &evt);
    }

    if (overflow) {
        uart_emit_error_evt(h, (uint32_t)UART_HAL_RES(RET_R_BUFFER_FULL));
    }
}

/**
 * @brief port 事件桥接回调
 * @param user UART 句柄
 * @param evt  port 事件
 */
static void uart_on_port_evt(void* user, const hal_uart_port_event_t* evt) {
    hal_uart_t* h = (hal_uart_t*)user;

    if ((h == NULL) || (evt == NULL)) return;

    switch (evt->type) {
        case HAL_UART_PORT_EVT_RX_READY:
            uart_on_rx_ready(h, &evt->rx.data);
            break;
        case HAL_UART_PORT_EVT_TX_DONE: {
            hal_uart_event_t hal_evt = {.type = HAL_UART_EVT_TX_DONE};
            hal_evt.tx.bytes         = evt->tx.bytes;
            uart_emit_evt(h, &hal_evt);
            break;
        }
        case HAL_UART_PORT_EVT_ERROR: {
            const ret_code_t rc_hal =
                uart_map_port_to_hal(evt->err.code, "hal_uart_port_evt", (uint32_t)h->id, 0u);
            uart_emit_error_evt(h, (uint32_t)rc_hal);
            break;
        }
        default:
            break;
    }
}

/**
 * @brief 初始化 UART 句柄并完成板级资源绑定
 * @param id   板级 UART 编号
 * @param cfg  UART 配置
 * @param out  返回 UART 句柄
 * @return RET_OK 或统一错误码
 */
ret_code_t hal_uart_init(hal_uart_id_t id, const hal_uart_cfg_t* cfg, hal_uart_t** out) {
    hal_uart_t* h                = NULL;
    hal_uart_port_handle_t* port = NULL;
    uint32_t rx_buffer_len       = 0u;
    ret_code_t rc                = RET_OK;

    ASSERT_PARAM((cfg != NULL) && (out != NULL));
    REQUIRE_RET((cfg != NULL) && (out != NULL), UART_HAL_PARAM(RET_R_INVALID_ARG));
    REQUIRE_RET(id < HAL_UART_ID_MAX, UART_HAL_PARAM(RET_R_RANGE_ERR));

    *out = NULL;
    h    = &g_uart_ctx[id];
    REQUIRE_RET(!h->initialized, UART_HAL_STATE(RET_R_BUSY));

    rc = hal_uart_port_init(id, cfg, &port);
    if (ret_is_err(rc)) return uart_map_port_to_hal(rc, "hal_uart_init", (uint32_t)id, 0u);

    rx_buffer_len = hal_uart_port_get_rx_buffer_len(port);
    if (rx_buffer_len < 2u) {
        (void)hal_uart_port_deinit(port);
        return UART_HAL_STATE(RET_R_STATE_ERR);
    }

    if (!h->rb_ready) {
        (void)snprintf(h->rx_rb_name, sizeof(h->rx_rb_name), "hal_uart_rx_%u", (unsigned)id);
        rc = CreateRingBuffer(&h->rx_rb, h->rx_rb_name, rx_buffer_len);
        if (ret_is_err(rc)) {
            (void)hal_uart_port_deinit(port);
            return UART_HAL_RES(RET_R_NO_MEM);
        }
        h->rb_ready = true;
    } else {
        if (h->rx_rb.size != rx_buffer_len) {
            (void)hal_uart_port_deinit(port);
            return UART_HAL_STATE(RET_R_STATE_ERR);
        }
        rc = ResetRingBuffer(&h->rx_rb);
        if (ret_is_err(rc)) {
            (void)hal_uart_port_deinit(port);
            return UART_HAL_STATE(RET_R_STATE_ERR);
        }
    }

    h->id          = id;
    h->port        = port;
    h->evt_cb      = NULL;
    h->evt_user    = NULL;
    h->initialized = true;

    rc = hal_uart_port_set_evt_cb(port, uart_on_port_evt, h);
    if (ret_is_err(rc)) {
        h->port        = NULL;
        h->initialized = false;
        (void)hal_uart_port_deinit(port);
        return uart_map_port_to_hal(rc, "hal_uart_init", (uint32_t)id, rx_buffer_len);
    }

    *out = h;
    return RET_OK;
}

/**
 * @brief 反初始化 UART 句柄并释放对应资源
 * @param h UART 句柄
 * @return RET_OK 或统一错误码
 */
ret_code_t hal_uart_deinit(hal_uart_t* h) {
    ret_code_t rc    = RET_OK;
    hal_uart_id_t id = HAL_UART_ID_MAX;

    ASSERT_PARAM(h != NULL);
    REQUIRE_RET(h != NULL, UART_HAL_PARAM(RET_R_INVALID_ARG));
    REQUIRE_RET(h->initialized, UART_HAL_STATE(RET_R_NOT_READY));

    id = h->id;
    (void)hal_uart_port_set_evt_cb(h->port, NULL, NULL);
    rc = hal_uart_port_deinit(h->port);
    if (ret_is_err(rc)) return uart_map_port_to_hal(rc, "hal_uart_deinit", (uint32_t)id, 0u);

    if (h->rb_ready) {
        (void)ResetRingBuffer(&h->rx_rb);
    }

    h->port        = NULL;
    h->evt_cb      = NULL;
    h->evt_user    = NULL;
    h->initialized = false;
    return RET_OK;
}

/**
 * @brief 启动 UART 接收路径
 * @param h UART 句柄
 * @return RET_OK 或统一错误码
 */
ret_code_t hal_uart_rx_start(hal_uart_t* h) {
    ret_code_t rc = RET_OK;

    ASSERT_PARAM(h != NULL);
    REQUIRE_RET(h != NULL, UART_HAL_PARAM(RET_R_INVALID_ARG));
    REQUIRE_RET(h->initialized, UART_HAL_STATE(RET_R_NOT_READY));

    rc = hal_uart_port_rx_start(h->port);
    if (ret_is_err(rc))
        return uart_map_port_to_hal(rc, "hal_uart_rx_start", (uint32_t)h->id, 0u);
    return RET_OK;
}

/**
 * @brief 异步发送数据
 * @param h   UART 句柄
 * @param buf 发送缓存地址
 * @param len 发送长度
 * @return RET_OK 或统一错误码
 */
ret_code_t hal_uart_send_async(hal_uart_t* h, const uint8_t* buf, uint32_t len) {
    ret_code_t rc = RET_OK;

    ASSERT_PARAM((h != NULL) && (buf != NULL) && (len != 0u));
    REQUIRE_RET((h != NULL) && (buf != NULL) && (len != 0u), UART_HAL_PARAM(RET_R_INVALID_ARG));
    REQUIRE_RET(h->initialized, UART_HAL_STATE(RET_R_NOT_READY));

    rc = hal_uart_port_send_async(h->port, buf, len);
    if (ret_is_err(rc))
        return uart_map_port_to_hal(rc, "hal_uart_send_async", (uint32_t)h->id, len);
    return RET_OK;
}

/**
 * @brief 从接收缓冲区拷贝读取数据
 * @param h     UART 句柄
 * @param out   输出缓冲区
 * @param want  期望读取字节数
 * @param nread 实际读取字节数
 * @return RET_OK 或统一错误码
 */
ret_code_t hal_uart_read(hal_uart_t* h, uint8_t* out, uint32_t want, uint32_t* nread) {
    hal_uart_read_span_t span = {0};
    RingBufferSpan rb_span    = {0};
    uint32_t granted          = 0u;
    ret_code_t rc             = RET_OK;

    if (nread != NULL) *nread = 0u;
    ASSERT_PARAM((h != NULL) && (out != NULL) && (nread != NULL));
    ASSERT_PARAM(want != 0u);
    REQUIRE_RET((h != NULL) && (out != NULL) && (nread != NULL), UART_HAL_PARAM(RET_R_INVALID_ARG));
    REQUIRE_RET(h->initialized, UART_HAL_STATE(RET_R_NOT_READY));
    REQUIRE_RET(want != 0u, UART_HAL_PARAM(RET_R_RANGE_ERR));

    rc = hal_uart_read_reserve(h, want, &span, &granted);
    if (ret_is_err(rc)) return rc;

    if (granted > 0u) {
        rb_span.p1 = (uint8_t*)span.p1;
        rb_span.n1 = span.n1;
        rb_span.p2 = (uint8_t*)span.p2;
        rb_span.n2 = span.n2;
        RingBuffer_SpanReadToLinear(&rb_span, out, granted);
        rc = hal_uart_read_commit(h, granted);
        if (ret_is_err(rc)) return rc;
    }

    *nread = granted;
    return RET_OK;
}

/**
 * @brief 申请接收缓冲区中的可读窗口
 * @param h     UART 句柄
 * @param want  期望读取字节数；传 0 表示尽可能多
 * @param out   返回可读窗口
 * @param nread 实际可读字节数
 * @return RET_OK 或统一错误码
 */
ret_code_t hal_uart_read_reserve(hal_uart_t* h, uint32_t want, hal_uart_read_span_t* out,
                                 uint32_t* nread) {
    RingBufferSpan span = {0};
    uint32_t granted    = 0u;
    uint32_t want_local = want;
    ret_code_t rc       = RET_OK;

    if (nread != NULL) *nread = 0u;
    ASSERT_PARAM((h != NULL) && (out != NULL) && (nread != NULL));
    REQUIRE_RET((h != NULL) && (out != NULL) && (nread != NULL), UART_HAL_PARAM(RET_R_INVALID_ARG));
    REQUIRE_RET(h->initialized, UART_HAL_STATE(RET_R_NOT_READY));

    if (want_local == 0u) {
        want_local = OSAL_in_isr() ? RingBuffer_GetUsedSizeFromISR(&h->rx_rb)
                                   : RingBuffer_GetUsedSize(&h->rx_rb);
        if (want_local == 0u) {
            out->p1 = NULL;
            out->p2 = NULL;
            out->n1 = 0u;
            out->n2 = 0u;
            *nread  = 0u;
            return RET_OK;
        }
    }

    /* 对端生产者是软件（CPU 写入），故 RB_SYNC_SMP。 */
    rc = RingBuffer_ReadReserve_SPSC(&h->rx_rb, want_local, &span, &granted, true, RB_SYNC_SMP);
    if (ret_is_err(rc)) return UART_HAL_STATE(RET_R_STATE_ERR);

    out->p1 = span.p1;
    out->p2 = span.p2;
    out->n1 = span.n1;
    out->n2 = span.n2;
    *nread  = granted;
    return RET_OK;
}

/**
 * @brief 提交零拷贝读取后已消费的字节数
 * @param h     UART 句柄
 * @param nread 已消费字节数
 * @return RET_OK 或统一错误码
 */
ret_code_t hal_uart_read_commit(hal_uart_t* h, uint32_t nread) {
    ret_code_t rc = RET_OK;

    ASSERT_PARAM(h != NULL);
    REQUIRE_RET(h != NULL, UART_HAL_PARAM(RET_R_INVALID_ARG));
    REQUIRE_RET(h->initialized, UART_HAL_STATE(RET_R_NOT_READY));
    if (nread == 0u) return RET_OK;

    rc = RingBuffer_ReadCommit_SPSC(&h->rx_rb, nread, RB_SYNC_SMP);
    if (ret_is_err(rc)) return UART_HAL_STATE(RET_R_STATE_ERR);
    return RET_OK;
}

/**
 * @brief 注册 UART 事件回调
 * @param h    UART 句柄
 * @param cb   事件回调
 * @param user 用户上下文
 * @return RET_OK
 */
ret_code_t hal_uart_set_evt_cb(hal_uart_t* h, hal_uart_evt_cb_t cb, void* user) {
    ASSERT_PARAM(h != NULL);
    REQUIRE_RET(h != NULL, UART_HAL_PARAM(RET_R_INVALID_ARG));
    REQUIRE_RET(h->initialized, UART_HAL_STATE(RET_R_NOT_READY));

    h->evt_cb   = cb;
    h->evt_user = user;
    return RET_OK;
}

#else /* !CFG_FEAT_HAL_UART */

void hal_uart_on_port_error(ret_code_t rc_port, ret_code_t rc_hal, const char* api, uint32_t arg0,
                            uint32_t arg1) {
    (void)rc_port;
    (void)rc_hal;
    (void)api;
    (void)arg0;
    (void)arg1;
}

ret_code_t hal_uart_init(hal_uart_id_t id, const hal_uart_cfg_t* cfg, hal_uart_t** out) {
    (void)id;
    (void)cfg;
    (void)out;
    return UART_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_uart_deinit(hal_uart_t* h) {
    (void)h;
    return UART_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_uart_rx_start(hal_uart_t* h) {
    (void)h;
    return UART_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_uart_send_async(hal_uart_t* h, const uint8_t* buf, uint32_t len) {
    (void)h;
    (void)buf;
    (void)len;
    return UART_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_uart_read(hal_uart_t* h, uint8_t* out, uint32_t want, uint32_t* nread) {
    (void)h;
    (void)out;
    (void)want;
    if (nread != NULL) *nread = 0u;
    return UART_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_uart_read_reserve(hal_uart_t* h, uint32_t want, hal_uart_read_span_t* out,
                                 uint32_t* nread) {
    (void)h;
    (void)want;
    (void)out;
    if (nread != NULL) *nread = 0u;
    return UART_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_uart_read_commit(hal_uart_t* h, uint32_t nread) {
    (void)h;
    (void)nread;
    return UART_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_uart_set_evt_cb(hal_uart_t* h, hal_uart_evt_cb_t cb, void* user) {
    (void)h;
    (void)cb;
    (void)user;
    return UART_HAL_PARAM(RET_R_UNSUPPORTED);
}

#endif /* CFG_FEAT_HAL_UART */
