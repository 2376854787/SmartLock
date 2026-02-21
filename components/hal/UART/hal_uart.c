#include "hal_uart.h"
#include "hal_uart_port.h"

#include <stddef.h>

#include "APP_config.h"
#include "log.h"

#if defined(ENABLE_HAL_UART)

#ifndef UART_HAL_RET
#define UART_HAL_RET(cls_, reason_) \
    RET_MAKE(RET_MOD_HAL, RET_SUB_HAL_UART, RET_CODE_MAKE((cls_), (reason_)))
#endif

__attribute__((weak)) void hal_uart_on_port_error(ret_code_t rc_port, const char* api,
                                                  hal_uart_id_t id, uint32_t arg0,
                                                  uint32_t arg1) {
    (void)rc_port;
    (void)api;
    (void)id;
    (void)arg0;
    (void)arg1;
    LOG_E("port", "port:%d, api:%s, uart_id:%d", rc_port, api, id);
}

static inline ret_code_t uart_map_port_to_hal(ret_code_t rc_port, const char* api, hal_uart_id_t id,
                                               uint32_t arg0, uint32_t arg1) {
    hal_uart_on_port_error(rc_port, api, id, arg0, arg1);

    if (ret_is_class(rc_port, RET_CLASS_PARAM)) {
        if (ret_is_reason(rc_port, RET_R_UNSUPPORTED))
            return UART_HAL_RET(RET_CLASS_PARAM, RET_R_UNSUPPORTED);
        return UART_HAL_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    }

    if (ret_is_class(rc_port, RET_CLASS_TIMEOUT)) {
        return UART_HAL_RET(RET_CLASS_TIMEOUT, RET_R_TIMEOUT);
    }

    if (ret_is_class(rc_port, RET_CLASS_RESOURCE)) {
        if (ret_is_reason(rc_port, RET_R_QUEUE_FULL))
            return UART_HAL_RET(RET_CLASS_RESOURCE, RET_R_QUEUE_FULL);
        if (ret_is_reason(rc_port, RET_R_BUFFER_FULL))
            return UART_HAL_RET(RET_CLASS_RESOURCE, RET_R_BUFFER_FULL);
        if (ret_is_reason(rc_port, RET_R_NO_MEM)) return UART_HAL_RET(RET_CLASS_RESOURCE, RET_R_NO_MEM);
        return UART_HAL_RET(RET_CLASS_RESOURCE, RET_R_NO_RESOURCE);
    }

    if (ret_is_class(rc_port, RET_CLASS_STATE)) {
        if (ret_is_reason(rc_port, RET_R_BUSY)) return UART_HAL_RET(RET_CLASS_STATE, RET_R_BUSY);
        if (ret_is_reason(rc_port, RET_R_NOT_READY))
            return UART_HAL_RET(RET_CLASS_STATE, RET_R_NOT_READY);
        return UART_HAL_RET(RET_CLASS_STATE, RET_R_STATE_ERR);
    }

    if (ret_is_class(rc_port, RET_CLASS_DATA)) {
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

#ifdef RET_R_INIT_FAIL
    return UART_HAL_RET(RET_CLASS_IO, RET_R_INIT_FAIL);
#else
    return UART_HAL_RET(RET_CLASS_IO, RET_R_IO);
#endif
}

ret_code_t hal_uart_open(hal_uart_id_t id, const hal_uart_cfg_t* cfg, hal_uart_t** out) {
    if (!cfg || !out) return UART_HAL_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);

    const ret_code_t rc = hal_uart_port_open(id, cfg, out);
    if (ret_is_err(rc)) {
        return uart_map_port_to_hal(rc, "hal_uart_open", id, 0u, 0u);
    }

    if (*out == NULL) return UART_HAL_RET(RET_CLASS_STATE, RET_R_STATE_ERR);
    return RET_OK;
}

ret_code_t hal_uart_close(hal_uart_t* h) {
    if (!h) return UART_HAL_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);

    const ret_code_t rc = hal_uart_port_close(h);
    if (ret_is_err(rc))
        return uart_map_port_to_hal(rc, "hal_uart_close", hal_uart_port_get_id(h), 0u, 0u);
    return RET_OK;
}

ret_code_t hal_uart_rx_start(hal_uart_t* h) {
    if (!h) return UART_HAL_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);

    const ret_code_t rc = hal_uart_port_rx_start(h);
    if (ret_is_err(rc))
        return uart_map_port_to_hal(rc, "hal_uart_rx_start", hal_uart_port_get_id(h), 0u, 0u);
    return RET_OK;
}

ret_code_t hal_uart_send_async(hal_uart_t* h, const uint8_t* buf, uint32_t len) {
    if (!h || !buf || len == 0u) return UART_HAL_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);

    const ret_code_t rc = hal_uart_port_send_async(h, buf, len);
    if (ret_is_err(rc))
        return uart_map_port_to_hal(rc, "hal_uart_send_async", hal_uart_port_get_id(h), len, 0u);
    return RET_OK;
}

ret_code_t hal_uart_read(hal_uart_t* h, uint8_t* out, uint32_t want, uint32_t* nread) {
    if (nread) *nread = 0u;
    if (!h || !out) return UART_HAL_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    if (want == 0u) return UART_HAL_RET(RET_CLASS_PARAM, RET_R_RANGE_ERR);

    const ret_code_t rc = hal_uart_port_read(h, out, want, nread);
    if (ret_is_err(rc))
        return uart_map_port_to_hal(rc, "hal_uart_read", hal_uart_port_get_id(h), want, 0u);
    return RET_OK;
}

ret_code_t hal_uart_read_reserve(hal_uart_t* h, uint32_t want, hal_uart_read_span_t* out,
                                 uint32_t* nread) {
    if (nread) *nread = 0u;
    if (!h || !out) return UART_HAL_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);

    const ret_code_t rc = hal_uart_port_read_reserve(h, want, out, nread);
    if (ret_is_err(rc))
        return uart_map_port_to_hal(rc, "hal_uart_read_reserve", hal_uart_port_get_id(h), want, 0u);
    return RET_OK;
}

ret_code_t hal_uart_read_commit(hal_uart_t* h, uint32_t nread) {
    if (!h) return UART_HAL_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);

    const ret_code_t rc = hal_uart_port_read_commit(h, nread);
    if (ret_is_err(rc))
        return uart_map_port_to_hal(rc, "hal_uart_read_commit", hal_uart_port_get_id(h), nread, 0u);
    return RET_OK;
}

ret_code_t hal_uart_set_evt_cb(hal_uart_t* h, hal_uart_evt_cb_t cb, void* user) {
    if (!h) return UART_HAL_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);

    const ret_code_t rc = hal_uart_port_set_evt_cb(h, cb, user);
    if (ret_is_err(rc))
        return uart_map_port_to_hal(rc, "hal_uart_set_evt_cb", hal_uart_port_get_id(h), 0u, 0u);
    return RET_OK;
}

#else /* !ENABLE_HAL_UART */

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

ret_code_t hal_uart_read_reserve(hal_uart_t* h, uint32_t want, hal_uart_read_span_t* out,
                                 uint32_t* nread) {
    (void)h;
    (void)want;
    (void)out;
    if (nread) *nread = 0u;
    return UART_HAL_RET(RET_CLASS_PARAM, RET_R_UNSUPPORTED);
}

ret_code_t hal_uart_read_commit(hal_uart_t* h, uint32_t nread) {
    (void)h;
    (void)nread;
    return UART_HAL_RET(RET_CLASS_PARAM, RET_R_UNSUPPORTED);
}

ret_code_t hal_uart_set_evt_cb(hal_uart_t* h, hal_uart_evt_cb_t cb, void* user) {
    (void)h;
    (void)cb;
    (void)user;
    return UART_HAL_RET(RET_CLASS_PARAM, RET_R_UNSUPPORTED);
}

#endif /* ENABLE_HAL_UART */
