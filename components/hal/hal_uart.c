#include "hal_uart.h"

#include <stddef.h>

#include "APP_config.h"
#include "RingBuffer.h"
#include "log.h"
#include "stm32_uart_bsp.h"

#if defined(ENABLE_HAL_UART)

/* ========== UART 上下文 ========== */
struct hal_uart {
    const char* name;         /*串口名称*/
    void* cb_user;            /* 用户上下文 */
    hal_uart_id_t id;         /*串口内部id*/
    stm32_uart_bsp_t bsp;     /* 串口映射配置 */
    RingBuffer rb;            /* 软件 RB：用于上层 read */
    uint32_t rx_last_pos;     /* 上次处理的 DMA 写指针 */
    volatile uint8_t tx_busy; /* 0/1 */
    uint32_t last_tx_len;     /* 上次DMA的位置 */
    bool isCompatible;        /* RB 严格模式/ 兼容模式 */
    hal_uart_evt_cb_t cb;     /* 事件回调函数 */
};
/* ========== port 实现========== */
ret_code_t hal_uart_port_open(hal_uart_id_t id, const hal_uart_cfg_t* cfg, hal_uart_t** out);
ret_code_t hal_uart_port_close(hal_uart_t* h);
ret_code_t hal_uart_port_rx_start(hal_uart_t* h);
ret_code_t hal_uart_port_send_async(hal_uart_t* h, const uint8_t* buf, uint32_t len);
ret_code_t hal_uart_port_read(hal_uart_t* h, uint8_t* out, uint32_t max, uint32_t* nread);
ret_code_t hal_uart_port_set_evt_cb(hal_uart_t* h, hal_uart_evt_cb_t cb, void* user);

/* ========== HAL 错误码构造（对外统一口径）========== */
#ifndef UART_HAL_RET
#define UART_HAL_RET(cls_, reason_) \
    RET_MAKE(RET_MOD_HAL, RET_SUB_HAL_UART, RET_CODE_MAKE((cls_), (reason_)))
#endif

/* ========== 商业做法：保留底层(port)原始错误码用于追踪 ========== */
/* 你可以在工程里提供强符号实现，把 rc_port 写入日志/事件环形缓冲/故障记录等 */
__attribute__((weak)) void hal_uart_on_port_error(ret_code_t rc_port, const char* api,
                                                  hal_uart_id_t id, uint32_t arg0, uint32_t arg1) {
    (void)rc_port;
    (void)api;
    (void)id;
    (void)arg0;
    (void)arg1;
    /* 默认空实现：建议接入日志/事件系统 */
    LOG_E("port", "port:%d, api:%s, uart_id:%d", rc_port, api, id);
}

/* ========== port -> HAL 错误码映射（关键：统一口径，不泄露 PORT）========== */
/* 规则：对外只暴露 HAL-UART 语义；底层细节用 hook 保留 */
static inline ret_code_t uart_map_port_to_hal(ret_code_t rc_port, const char* api, hal_uart_id_t id,
                                              uint32_t arg0, uint32_t arg1) {
    /* 记录原始 port 错误码 */
    hal_uart_on_port_error(rc_port, api, id, arg0, arg1);

    /* 1) 直接按 class 映射（对上层策略最稳定） */
    if (ret_is_class(rc_port, RET_CLASS_PARAM)) {
        /* 更细可按 reason 决定 INVALID_ARG/UNSUPPORTED 等 */
        if (ret_is_reason(rc_port, RET_R_UNSUPPORTED))
            return UART_HAL_RET(RET_CLASS_PARAM, RET_R_UNSUPPORTED);
        return UART_HAL_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    }

    if (ret_is_class(rc_port, RET_CLASS_TIMEOUT)) {
        return UART_HAL_RET(RET_CLASS_TIMEOUT, RET_R_TIMEOUT);
    }

    if (ret_is_class(rc_port, RET_CLASS_RESOURCE)) {
        /* 资源类：优先保持语义（队列满/缓冲满/内存不足） */
        if (ret_is_reason(rc_port, RET_R_QUEUE_FULL))
            return UART_HAL_RET(RET_CLASS_RESOURCE, RET_R_QUEUE_FULL);
        if (ret_is_reason(rc_port, RET_R_BUFFER_FULL))
            return UART_HAL_RET(RET_CLASS_RESOURCE, RET_R_BUFFER_FULL);
        if (ret_is_reason(rc_port, RET_R_NO_MEM))
            return UART_HAL_RET(RET_CLASS_RESOURCE, RET_R_NO_MEM);
        return UART_HAL_RET(RET_CLASS_RESOURCE, RET_R_NO_RESOURCE);
    }

    if (ret_is_class(rc_port, RET_CLASS_STATE)) {
        /* 状态类：busy / not_ready / state_err */
        if (ret_is_reason(rc_port, RET_R_BUSY)) return UART_HAL_RET(RET_CLASS_STATE, RET_R_BUSY);
        if (ret_is_reason(rc_port, RET_R_NOT_READY))
            return UART_HAL_RET(RET_CLASS_STATE, RET_R_NOT_READY);
        return UART_HAL_RET(RET_CLASS_STATE, RET_R_STATE_ERR);
    }

    if (ret_is_class(rc_port, RET_CLASS_DATA)) {
        /* 数据类：解析/CRC/数据不足/溢出 */
        if (ret_is_reason(rc_port, RET_R_CRC)) return UART_HAL_RET(RET_CLASS_DATA, RET_R_CRC);
        if (ret_is_reason(rc_port, RET_R_DATA_NOT_ENOUGH))
            return UART_HAL_RET(RET_CLASS_DATA, RET_R_DATA_NOT_ENOUGH);
        if (ret_is_reason(rc_port, RET_R_DATA_OVERFLOW))
            return UART_HAL_RET(RET_CLASS_DATA, RET_R_DATA_OVERFLOW);
        return UART_HAL_RET(RET_CLASS_DATA, RET_R_PARSE_ERR);
    }

    if (ret_is_class(rc_port, RET_CLASS_FATAL)) {
        return UART_HAL_RET(RET_CLASS_FATAL, RET_R_PANIC);
    }

    /* 2) 剩余全部归为 IO（最常见的底层失败） */
    /* 建议你在 ret_reason_t 里新增 RET_R_INIT_FAIL，用于 init/enable/start 类失败 */
#ifdef RET_R_INIT_FAIL
    return UART_HAL_RET(RET_CLASS_IO, RET_R_INIT_FAIL);
#else
    return UART_HAL_RET(RET_CLASS_IO, RET_R_IO);
#endif
}

/* ========== HAL API（对外）========== */

ret_code_t hal_uart_open(hal_uart_id_t id, const hal_uart_cfg_t* cfg, hal_uart_t** out) {
    /* HAL 契约校验：参数错误由 HAL 统一返回 */
    if (!cfg || !out) return UART_HAL_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);

    const ret_code_t rc = hal_uart_port_open(id, cfg, out);
    if (ret_is_err(rc)) {
        return uart_map_port_to_hal(rc, "hal_uart_open", id, 0u, 0u);
    }

    /* HAL 契约：open 成功 out 必须有效 */
    if (*out == NULL) return UART_HAL_RET(RET_CLASS_STATE, RET_R_STATE_ERR);

    return RET_OK;
}

ret_code_t hal_uart_close(hal_uart_t* h) {
    if (!h) return UART_HAL_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);

    const ret_code_t rc = hal_uart_port_close(h);
    if (ret_is_err(rc))
        return uart_map_port_to_hal(rc, "hal_uart_close", (hal_uart_id_t)h->id, 0u, 0u);
    return RET_OK;
}

ret_code_t hal_uart_rx_start(hal_uart_t* h) {
    if (!h) return UART_HAL_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);

    const ret_code_t rc = hal_uart_port_rx_start(h);
    if (ret_is_err(rc))
        return uart_map_port_to_hal(rc, "hal_uart_rx_start", (hal_uart_id_t)h->id, 0u, 0u);
    return RET_OK;
}

ret_code_t hal_uart_send_async(hal_uart_t* h, const uint8_t* buf, uint32_t len) {
    if (!h || !buf || len == 0u) return UART_HAL_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);

    const ret_code_t rc = hal_uart_port_send_async(h, buf, len);
    if (ret_is_err(rc))
        return uart_map_port_to_hal(rc, "hal_uart_send_async", (hal_uart_id_t)h->id, len, 0u);
    return RET_OK;
}

ret_code_t hal_uart_read(hal_uart_t* h, uint8_t* out, uint32_t want, uint32_t* nread) {
    if (nread) *nread = 0u;
    if (!h || !out) return UART_HAL_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    if (want == 0u) return UART_HAL_RET(RET_CLASS_PARAM, RET_R_RANGE_ERR);

    const ret_code_t rc = hal_uart_port_read(h, out, want, nread);
    if (ret_is_err(rc))
        return uart_map_port_to_hal(rc, "hal_uart_read", (hal_uart_id_t)h->id, want, 0u);
    return RET_OK;
}

ret_code_t hal_uart_set_evt_cb(hal_uart_t* h, hal_uart_evt_cb_t cb, void* user) {
    if (!h) return UART_HAL_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    /* cb 是否允许为 NULL：按你的契约决定。若不允许就打开这行 */
    /* if (!cb) return UART_HAL_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG); */

    const ret_code_t rc = hal_uart_port_set_evt_cb(h, cb, user);
    if (ret_is_err(rc))
        return uart_map_port_to_hal(rc, "hal_uart_set_evt_cb", (hal_uart_id_t)h->id, 0u, 0u);
    return RET_OK;
}

#else /* !ENABLE_HAL_UART */

/* 未启用 HAL-UART：对外也保持 HAL 模块口径（商业一致性） */
#ifndef UART_HAL_RET
#define UART_HAL_RET(cls_, reason_) \
    RET_MAKE3(RET_MOD_HAL, RET_SUB_HAL_UART, RET_CODE_MAKE((cls_), (reason_)))
#endif

ret_code_t hal_uart_open(hal_uart_id_t id, const hal_uart_cfg_t* cfg, hal_uart_t** out) {
    (void)id;
    (void)cfg;
    (void)out;
    return UART_HAL_RET(RET_CLASS_PARAM, RET_R_UNSUPPORTED);
}

ret_code_t hal_uart_close(hal_uart_t* h) {
    (void)h;
    return UART_HAL_RET(RET_CLASS_PARAM, RET_R_UNSUPPORTED);
}

ret_code_t hal_uart_rx_start(hal_uart_t* h) {
    (void)h;
    return UART_HAL_RET(RET_CLASS_PARAM, RET_R_UNSUPPORTED);
}

ret_code_t hal_uart_send_async(hal_uart_t* h, const uint8_t* buf, uint32_t len) {
    (void)h;
    (void)buf;
    (void)len;
    return UART_HAL_RET(RET_CLASS_PARAM, RET_R_UNSUPPORTED);
}

ret_code_t hal_uart_read(hal_uart_t* h, uint8_t* out, uint32_t want, uint32_t* nread) {
    (void)h;
    (void)out;
    (void)want;
    if (nread) *nread = 0u;
    return UART_HAL_RET(RET_CLASS_PARAM, RET_R_UNSUPPORTED);
}

ret_code_t hal_uart_set_evt_cb(hal_uart_t* h, hal_uart_evt_cb_t cb, void* user) {
    (void)h;
    (void)cb;
    (void)user;
    return UART_HAL_RET(RET_CLASS_PARAM, RET_R_UNSUPPORTED);
}

#endif /* ENABLE_HAL_UART */
