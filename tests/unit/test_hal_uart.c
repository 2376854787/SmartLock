#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "RingBuffer.h"
#include "hal_uart.h"
#include "hal_uart_port.h"

#define TEST_RB_RET(clas_, reason_) \
    RET_MAKE(RET_MOD_RB, RET_SUB_RB_CORE, RET_CODE_MAKE((clas_), (reason_)))
#define TEST_PORT_RET(clas_, reason_) \
    RET_MAKE(RET_MOD_PORT, RET_SUB_PORT_UART, RET_CODE_MAKE((clas_), (reason_)))

struct hal_uart_port_handle {
    hal_uart_id_t id;
    uint32_t rx_buffer_len;
    bool initialized;
    hal_uart_port_evt_cb_t cb;
    void* user;
};

static struct hal_uart_port_handle g_port = {0};
static uint32_t g_rx_evt_bytes            = 0u;
static uint32_t g_tx_evt_bytes            = 0u;
static uint32_t g_err_evt_flags           = 0u;
static uint32_t g_evt_count               = 0u;

static uint32_t rb_used(const RingBuffer* rb) {
    return (rb->rear_index + rb->size - rb->front_index) % rb->size;
}

static uint32_t rb_remain(const RingBuffer* rb) {
    return (rb->size - 1u) - rb_used(rb);
}

static void rb_make_span(RingBuffer* rb, uint32_t index, uint32_t len, RingBufferSpan* out) {
    uint32_t first = 0u;

    out->p1 = NULL;
    out->n1 = 0u;
    out->p2 = NULL;
    out->n2 = 0u;
    if (len == 0u) return;

    first = rb->size - index;
    if (first > len) first = len;

    out->p1 = &rb->buffer[index];
    out->n1 = first;
    if (len > first) {
        out->p2 = &rb->buffer[0];
        out->n2 = len - first;
    }
}

ret_code_t CreateRingBuffer(RingBuffer* rb, const char* name, uint32_t size) {
    (void)name;
    if ((rb == NULL) || (size < 2u)) return TEST_RB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    rb->buffer = (uint8_t*)malloc(size);
    if (rb->buffer == NULL) return TEST_RB_RET(RET_CLASS_RESOURCE, RET_R_NO_MEM);
    rb->size        = size;
    rb->front_index = 0u;
    rb->rear_index  = 0u;
    return RET_OK;
}

ret_code_t ResetRingBuffer(RingBuffer* rb) {
    if ((rb == NULL) || (rb->buffer == NULL)) return TEST_RB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    rb->front_index = 0u;
    rb->rear_index  = 0u;
    return RET_OK;
}

uint32_t RingBuffer_GetUsedSize(const RingBuffer* rb) {
    return (rb == NULL) ? 0u : rb_used(rb);
}

uint32_t RingBuffer_GetUsedSizeFromISR(const RingBuffer* rb) {
    return RingBuffer_GetUsedSize(rb);
}

ret_code_t RingBuffer_WriteReserve_SPSC(RingBuffer* rb, uint32_t want, RingBufferSpan* out,
                                        uint32_t* granted, bool isCompatible, rb_sync_t sync) {
    uint32_t remain = 0u;
    (void)sync;

    if ((rb == NULL) || (out == NULL) || (granted == NULL) || (want == 0u)) {
        return TEST_RB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    }

    remain   = rb_remain(rb);
    *granted = (want <= remain) ? want : (isCompatible ? remain : 0u);
    if (!isCompatible && (*granted != want)) {
        return TEST_RB_RET(RET_CLASS_RESOURCE, RET_R_BUFFER_FULL);
    }

    rb_make_span(rb, rb->rear_index, *granted, out);
    return RET_OK;
}

ret_code_t RingBuffer_WriteCommit_SPSC(RingBuffer* rb, uint32_t commit, rb_sync_t sync) {
    (void)sync;
    if ((rb == NULL) || (rb->buffer == NULL) || (commit > rb_remain(rb))) {
        return TEST_RB_RET(RET_CLASS_STATE, RET_R_STATE_ERR);
    }
    rb->rear_index = (rb->rear_index + commit) % rb->size;
    return RET_OK;
}

ret_code_t RingBuffer_ReadReserve_SPSC(RingBuffer* rb, uint32_t want, RingBufferSpan* out,
                                       uint32_t* granted, bool isCompatible, rb_sync_t sync) {
    uint32_t used = 0u;
    (void)sync;

    if ((rb == NULL) || (out == NULL) || (granted == NULL) || (want == 0u)) {
        return TEST_RB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    }

    used      = rb_used(rb);
    *granted  = (want <= used) ? want : (isCompatible ? used : 0u);
    if (!isCompatible && (*granted != want)) {
        return TEST_RB_RET(RET_CLASS_DATA, RET_R_DATA_NOT_ENOUGH);
    }

    rb_make_span(rb, rb->front_index, *granted, out);
    return RET_OK;
}

ret_code_t RingBuffer_ReadCommit_SPSC(RingBuffer* rb, uint32_t commit, rb_sync_t sync) {
    (void)sync;
    if ((rb == NULL) || (rb->buffer == NULL) || (commit > rb_used(rb))) {
        return TEST_RB_RET(RET_CLASS_STATE, RET_R_STATE_ERR);
    }
    rb->front_index = (rb->front_index + commit) % rb->size;
    return RET_OK;
}

void RingBuffer_SpanWriteFromLinear(const RingBufferSpan* span, const uint8_t* src, uint32_t len) {
    uint32_t copied = 0u;

    if ((span == NULL) || (src == NULL)) return;
    if (span->n1 > 0u) {
        const uint32_t copy1 = (len < span->n1) ? len : span->n1;
        memcpy(span->p1, src, copy1);
        copied = copy1;
    }
    if ((len > copied) && (span->n2 > 0u)) {
        memcpy(span->p2, src + copied, len - copied);
    }
}

void RingBuffer_SpanReadToLinear(const RingBufferSpan* span, uint8_t* dst, uint32_t len) {
    uint32_t copied = 0u;

    if ((span == NULL) || (dst == NULL)) return;
    if (span->n1 > 0u) {
        const uint32_t copy1 = (len < span->n1) ? len : span->n1;
        memcpy(dst, span->p1, copy1);
        copied = copy1;
    }
    if ((len > copied) && (span->n2 > 0u)) {
        memcpy(dst + copied, span->p2, len - copied);
    }
}

ret_code_t hal_uart_port_init(hal_uart_id_t id, const hal_uart_cfg_t* cfg,
                              hal_uart_port_handle_t** out) {
    (void)cfg;
    if ((out == NULL) || (id >= HAL_UART_ID_MAX)) return TEST_PORT_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    g_port.id            = id;
    g_port.rx_buffer_len = 16u;
    g_port.initialized   = true;
    g_port.cb            = NULL;
    g_port.user          = NULL;
    *out                 = &g_port;
    return RET_OK;
}

ret_code_t hal_uart_port_deinit(hal_uart_port_handle_t* h) {
    if ((h == NULL) || !h->initialized) return TEST_PORT_RET(RET_CLASS_STATE, RET_R_NOT_READY);
    h->initialized = false;
    h->cb          = NULL;
    h->user        = NULL;
    return RET_OK;
}

ret_code_t hal_uart_port_rx_start(hal_uart_port_handle_t* h) {
    return ((h != NULL) && h->initialized) ? RET_OK
                                           : TEST_PORT_RET(RET_CLASS_STATE, RET_R_NOT_READY);
}

ret_code_t hal_uart_port_send_async(hal_uart_port_handle_t* h, const uint8_t* buf, uint32_t len) {
    (void)buf;
    if ((h == NULL) || !h->initialized || (len == 0u)) {
        return TEST_PORT_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    }
    return RET_OK;
}

ret_code_t hal_uart_port_set_evt_cb(hal_uart_port_handle_t* h, hal_uart_port_evt_cb_t cb,
                                    void* user) {
    if ((h == NULL) || !h->initialized) return TEST_PORT_RET(RET_CLASS_STATE, RET_R_NOT_READY);
    h->cb   = cb;
    h->user = user;
    return RET_OK;
}

hal_uart_id_t hal_uart_port_get_id(const hal_uart_port_handle_t* h) {
    return (h == NULL) ? HAL_UART_ID_MAX : h->id;
}

uint32_t hal_uart_port_get_rx_buffer_len(const hal_uart_port_handle_t* h) {
    return (h == NULL) ? 0u : h->rx_buffer_len;
}

static void emit_port_rx(const uint8_t* data, uint32_t len) {
    hal_uart_port_event_t evt = {.type = HAL_UART_PORT_EVT_RX_READY};
    evt.rx.data.p1            = data;
    evt.rx.data.n1            = len;
    evt.rx.data.p2            = NULL;
    evt.rx.data.n2            = 0u;
    g_port.cb(g_port.user, &evt);
}

static void emit_port_tx_done(uint32_t bytes) {
    hal_uart_port_event_t evt = {.type = HAL_UART_PORT_EVT_TX_DONE};
    evt.tx.bytes              = bytes;
    g_port.cb(g_port.user, &evt);
}

static void emit_port_error(ret_code_t code) {
    hal_uart_port_event_t evt = {.type = HAL_UART_PORT_EVT_ERROR};
    evt.err.code              = code;
    g_port.cb(g_port.user, &evt);
}

static void test_evt_cb(void* user, const hal_uart_event_t* evt) {
    (void)user;
    ++g_evt_count;
    switch (evt->type) {
        case HAL_UART_EVT_RX:
            g_rx_evt_bytes = evt->rx.bytes;
            break;
        case HAL_UART_EVT_TX_DONE:
            g_tx_evt_bytes = evt->tx.bytes;
            break;
        case HAL_UART_EVT_ERROR:
            g_err_evt_flags = evt->err.flags;
            break;
        default:
            break;
    }
}

int main(void) {
    hal_uart_t* uart              = NULL;
    uint8_t read_buf[8]           = {0};
    uint32_t nread                = 0u;
    hal_uart_read_span_t span     = {0};
    const hal_uart_cfg_t cfg = {
        .baud      = 115200u,
        .data_bits = HAL_UART_DATA_BITS_8,
        .stop_bits = HAL_UART_STOP_BITS_1,
        .parity    = HAL_UART_PARITY_NONE,
        .flow_ctrl = false,
    };

    assert(ret_is_ok(hal_uart_init(HAL_UART_ID_1, &cfg, &uart)));
    assert(uart != NULL);
    assert(ret_is_ok(hal_uart_set_evt_cb(uart, test_evt_cb, NULL)));
    assert(ret_is_ok(hal_uart_rx_start(uart)));

    emit_port_rx((const uint8_t*)"AT\r\n", 4u);
    assert(g_evt_count == 1u);
    assert(g_rx_evt_bytes == 4u);

    assert(ret_is_ok(hal_uart_read_reserve(uart, 0u, &span, &nread)));
    assert(nread == 4u);
    assert(span.n1 == 4u);
    assert(memcmp(span.p1, "AT\r\n", 4u) == 0);
    assert(ret_is_ok(hal_uart_read_commit(uart, nread)));

    emit_port_rx((const uint8_t*)"OK\r\n", 4u);
    assert(ret_is_ok(hal_uart_read(uart, read_buf, sizeof(read_buf), &nread)));
    assert(nread == 4u);
    assert(memcmp(read_buf, "OK\r\n", 4u) == 0);

    emit_port_tx_done(3u);
    assert(g_tx_evt_bytes == 3u);

    emit_port_error(TEST_PORT_RET(RET_CLASS_STATE, RET_R_BUSY));
    assert(RET_MODULE(g_err_evt_flags) == RET_MOD_HAL);
    assert(RET_SUBMODULE(g_err_evt_flags) == RET_SUB_HAL_UART);
    assert(ret_is_class(g_err_evt_flags, RET_CLASS_STATE));
    assert(ret_is_reason(g_err_evt_flags, RET_R_BUSY));

    assert(ret_is_ok(hal_uart_deinit(uart)));

    puts("test_hal_uart: PASS");
    return 0;
}
