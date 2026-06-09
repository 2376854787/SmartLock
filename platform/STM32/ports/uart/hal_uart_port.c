#include "APP_config.h"
#include "stm32_hal_config.h"

#if (defined(CFG_TARGET_PLATFORM_STM32_HAL) && (CFG_TARGET_PLATFORM_STM32_HAL == 1)) && \
    (defined(CFG_FEAT_HAL_UART) && (CFG_FEAT_HAL_UART == 1))

#include <string.h>

#include "assert_cus.h"
#include "hal_uart.h"
#include "hal_uart_port.h"
#include "osal.h"
#include "ret_code_t.h"
#include "stm32_hal.h"
#include "stm32_uart_bsp.h"
#include "stm32_uart_series.h"

#define UART_RET(clas_, errno_) \
    RET_MAKE(RET_MOD_PORT, RET_SUB_PORT_UART, RET_CODE_MAKE((clas_), (errno_)))

struct hal_uart_port_handle {
    hal_uart_id_t id;
    stm32_uart_bsp_t bsp;
    hal_uart_port_evt_cb_t evt_cb;
    void* evt_user;
    bool initialized;
    uint32_t rx_last_pos;
    uint32_t last_tx_len;
    volatile uint8_t tx_busy;
};

static struct hal_uart_port_handle g_uart_ports[HAL_UART_ID_MAX];

__WEAK void stm32_uart_dma_tx_clean(const void* ptr, uint32_t len) {
    (void)ptr;
    (void)len;
}

__WEAK void stm32_uart_dma_rx_invalidate(const void* ptr, uint32_t len) {
    (void)ptr;
    (void)len;
}

static inline bool is_power_of_two_size(uint32_t size) {
    return (size != 0u) && ((size & (size - 1u)) == 0u);
}

static ret_code_t uart_map_hal_error(uint32_t hal_err) {
#if defined(HAL_UART_ERROR_ORE)
    if ((hal_err & HAL_UART_ERROR_ORE) != 0u) {
        return UART_RET(RET_CLASS_DATA, RET_R_DATA_OVERFLOW);
    }
#endif
    (void)hal_err;
    return UART_RET(RET_CLASS_IO, RET_R_IO);
}

static ret_code_t uart_parity_to_hw(hal_uart_parity_t parity, uint32_t* out) {
    REQUIRE_RET(out != NULL, UART_RET(RET_CLASS_PARAM, RET_R_NULL_PTR));

    switch (parity) {
        case HAL_UART_PARITY_NONE:
            *out = UART_PARITY_NONE;
            return RET_OK;
        case HAL_UART_PARITY_EVEN:
            *out = UART_PARITY_EVEN;
            return RET_OK;
        case HAL_UART_PARITY_ODD:
            *out = UART_PARITY_ODD;
            return RET_OK;
        default:
            return UART_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    }
}

static ret_code_t uart_stop_bits_to_hw(hal_uart_stop_bits_t stop_bits, uint32_t* out) {
    REQUIRE_RET(out != NULL, UART_RET(RET_CLASS_PARAM, RET_R_NULL_PTR));

    switch (stop_bits) {
        case HAL_UART_STOP_BITS_1:
            *out = UART_STOPBITS_1;
            return RET_OK;
        case HAL_UART_STOP_BITS_2:
            *out = UART_STOPBITS_2;
            return RET_OK;
        default:
            return UART_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    }
}

static ret_code_t uart_word_length_to_hw(const hal_uart_cfg_t* cfg, uint32_t* out) {
    REQUIRE_RET((cfg != NULL) && (out != NULL), UART_RET(RET_CLASS_PARAM, RET_R_NULL_PTR));

    switch (cfg->data_bits) {
        case HAL_UART_DATA_BITS_8:
            *out = (cfg->parity == HAL_UART_PARITY_NONE) ? UART_WORDLENGTH_8B
                                                         : UART_WORDLENGTH_9B;
            return RET_OK;
        case HAL_UART_DATA_BITS_9:
            REQUIRE_RET(cfg->parity == HAL_UART_PARITY_NONE,
                        UART_RET(RET_CLASS_PARAM, RET_R_UNSUPPORTED));
            *out = UART_WORDLENGTH_9B;
            return RET_OK;
        default:
            return UART_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    }
}

static struct hal_uart_port_handle* uart_find_by_huart(const UART_HandleTypeDef* huart) {
    uint32_t idx = 0u;

    if (huart == NULL) return NULL;
    for (idx = 0u; idx < HAL_UART_ID_MAX; ++idx) {
        if (g_uart_ports[idx].initialized && (g_uart_ports[idx].bsp.huart == huart)) {
            return &g_uart_ports[idx];
        }
    }
    return NULL;
}

static inline uint32_t uart_dma_pos(const hal_uart_port_handle_t* h) {
    const uint32_t ndtr = __HAL_DMA_GET_COUNTER(h->bsp.hdma_rx);
    return (h->bsp.rx_dma_len - ndtr) % h->bsp.rx_dma_len;
}

static void uart_emit_evt(const hal_uart_port_handle_t* h, const hal_uart_port_event_t* evt) {
    if ((h != NULL) && (evt != NULL) && (h->evt_cb != NULL)) {
        h->evt_cb(h->evt_user, evt);
    }
}

static void uart_emit_rx_delta(hal_uart_port_handle_t* h) {
    const uint32_t len = h->bsp.rx_dma_len;
    const uint32_t pos = uart_dma_pos(h);
    const uint32_t last = h->rx_last_pos;
    const uint32_t delta = (pos + len - last) & (len - 1u);
    hal_uart_port_event_t evt = {.type = HAL_UART_PORT_EVT_RX_READY};
    uint32_t first = 0u;

    if (pos == last) return;
    if (delta == 0u) return;

    stm32_uart_dma_rx_invalidate(h->bsp.rx_dma_buf, h->bsp.rx_dma_len);

    first = len - last;
    if (first > delta) first = delta;

    evt.rx.data.p1 = &h->bsp.rx_dma_buf[last];
    evt.rx.data.n1 = first;
    evt.rx.data.p2 = NULL;
    evt.rx.data.n2 = 0u;
    if (delta > first) {
        evt.rx.data.p2 = &h->bsp.rx_dma_buf[0];
        evt.rx.data.n2 = delta - first;
    }

    h->rx_last_pos = pos;
    uart_emit_evt(h, &evt);
}

static inline uint32_t uart_take_tx_len_and_clear_busy(hal_uart_port_handle_t* h) {
    osal_crit_state_t cs = 0u;
    uint32_t tx_len = 0u;

    OSAL_enter_critical_ex(&cs);
    tx_len = h->last_tx_len;
    h->last_tx_len = 0u;
    h->tx_busy = 0u;
    OSAL_exit_critical_ex(cs);
    return tx_len;
}

void hal_uart_txCp_case(const UART_HandleTypeDef* huart) {
    hal_uart_port_handle_t* h = uart_find_by_huart(huart);
    hal_uart_port_event_t evt = {.type = HAL_UART_PORT_EVT_TX_DONE};

    if (h == NULL) return;

    evt.tx.bytes = uart_take_tx_len_and_clear_busy(h);
    uart_emit_evt(h, &evt);
}

void hal_uart_error_case(const UART_HandleTypeDef* huart) {
    hal_uart_port_handle_t* h = uart_find_by_huart(huart);
    hal_uart_port_event_t evt = {.type = HAL_UART_PORT_EVT_ERROR};

    if (h == NULL) return;

    (void)uart_take_tx_len_and_clear_busy(h);
    evt.err.code = uart_map_hal_error((uint32_t)huart->ErrorCode);
    uart_emit_evt(h, &evt);
}

void hal_uart_rx_event_case(const UART_HandleTypeDef* huart, uint16_t Size) {
    hal_uart_port_handle_t* h = uart_find_by_huart(huart);

    (void)Size;
    if (h == NULL) return;
    uart_emit_rx_delta(h);
}

void hal_uart_rx_dma_progress_case(const UART_HandleTypeDef* huart) {
    hal_uart_port_handle_t* h = uart_find_by_huart(huart);

    if (h == NULL) return;
    uart_emit_rx_delta(h);
}

static void uart_handle_idle_irq(hal_uart_port_handle_t* h) {
#if defined(UART_FLAG_IDLE)
    if (__HAL_UART_GET_FLAG(h->bsp.huart, UART_FLAG_IDLE) != RESET) {
        __HAL_UART_CLEAR_IDLEFLAG(h->bsp.huart);
        uart_emit_rx_delta(h);
    }
#else
    (void)h;
#endif
}

void stm32_uart_irq_usart(hal_uart_id_t id) {
    hal_uart_port_handle_t* h = NULL;

    if (id >= HAL_UART_ID_MAX) return;
    h = &g_uart_ports[id];
    if (!h->initialized || (h->bsp.huart == NULL)) return;

    HAL_UART_IRQHandler(h->bsp.huart);
    uart_handle_idle_irq(h);
}

void stm32_uart_irq_dma_rx(hal_uart_id_t id) {
    hal_uart_port_handle_t* h = NULL;

    if (id >= HAL_UART_ID_MAX) return;
    h = &g_uart_ports[id];
    if (!h->initialized || (h->bsp.hdma_rx == NULL)) return;

    HAL_DMA_IRQHandler(h->bsp.hdma_rx);
}

void stm32_uart_irq_dma_tx(hal_uart_id_t id) {
    hal_uart_port_handle_t* h = NULL;

    if (id >= HAL_UART_ID_MAX) return;
    h = &g_uart_ports[id];
    if (!h->initialized || (h->bsp.hdma_tx == NULL)) return;

    HAL_DMA_IRQHandler(h->bsp.hdma_tx);
}

ret_code_t hal_uart_port_init(hal_uart_id_t id, const hal_uart_cfg_t* cfg,
                              hal_uart_port_handle_t** out) {
    hal_uart_port_handle_t* h = NULL;
    stm32_uart_bsp_t bsp = {0};
    uint32_t parity = 0u;
    uint32_t stop_bits = 0u;
    uint32_t word_length = 0u;
    ret_code_t rc = RET_OK;

    REQUIRE_RET((cfg != NULL) && (out != NULL), UART_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG));
    REQUIRE_RET(id < HAL_UART_ID_MAX, UART_RET(RET_CLASS_PARAM, RET_R_RANGE_ERR));

    h = &g_uart_ports[id];
    REQUIRE_RET(!h->initialized, UART_RET(RET_CLASS_STATE, RET_R_BUSY));

    rc = stm32_uart_bsp_get(id, &bsp);
    if (ret_is_err(rc)) return rc;

    REQUIRE_RET((bsp.huart != NULL) && (bsp.hdma_rx != NULL) && (bsp.hdma_tx != NULL) &&
                    (bsp.rx_dma_buf != NULL) && (bsp.rx_dma_len >= 2u) &&
                    is_power_of_two_size(bsp.rx_dma_len) && (bsp.hal_rx_buffer_len >= 2u),
                UART_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG));

    rc = uart_parity_to_hw(cfg->parity, &parity);
    if (ret_is_err(rc)) return rc;
    rc = uart_stop_bits_to_hw(cfg->stop_bits, &stop_bits);
    if (ret_is_err(rc)) return rc;
    rc = uart_word_length_to_hw(cfg, &word_length);
    if (ret_is_err(rc)) return rc;

    memset(h, 0, sizeof(*h));
    h->id = id;
    h->bsp = bsp;

    h->bsp.huart->Init.BaudRate = cfg->baud;
    h->bsp.huart->Init.HwFlowCtl =
        cfg->flow_ctrl ? UART_HWCONTROL_RTS_CTS : UART_HWCONTROL_NONE;
    h->bsp.huart->Init.Parity = parity;
    h->bsp.huart->Init.StopBits = stop_bits;
    h->bsp.huart->Init.WordLength = word_length;

    if (HAL_UART_Init(h->bsp.huart) != HAL_OK) {
        return UART_RET(RET_CLASS_IO, RET_R_IO);
    }

    if (h->bsp.usart_irq > 0) {
        HAL_NVIC_SetPriority(h->bsp.usart_irq, h->bsp.irq_prio, h->bsp.irq_sub_prio);
        HAL_NVIC_EnableIRQ(h->bsp.usart_irq);
    }
    if (h->bsp.dma_rx_irq > 0) {
        HAL_NVIC_SetPriority(h->bsp.dma_rx_irq, h->bsp.irq_prio, h->bsp.irq_sub_prio);
        HAL_NVIC_EnableIRQ(h->bsp.dma_rx_irq);
    }
    if (h->bsp.dma_tx_irq > 0) {
        HAL_NVIC_SetPriority(h->bsp.dma_tx_irq, h->bsp.irq_prio, h->bsp.irq_sub_prio);
        HAL_NVIC_EnableIRQ(h->bsp.dma_tx_irq);
    }

    h->rx_last_pos = 0u;
    h->tx_busy = 0u;
    h->last_tx_len = 0u;
    h->initialized = true;
    *out = h;
    return RET_OK;
}

ret_code_t hal_uart_port_deinit(hal_uart_port_handle_t* h) {
    REQUIRE_RET(h != NULL, UART_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG));
    REQUIRE_RET(h->initialized, UART_RET(RET_CLASS_STATE, RET_R_NOT_READY));

    (void)HAL_UART_DMAStop(h->bsp.huart);
    if (HAL_UART_DeInit(h->bsp.huart) != HAL_OK) {
        return UART_RET(RET_CLASS_IO, RET_R_IO);
    }

    h->evt_cb = NULL;
    h->evt_user = NULL;
    h->initialized = false;
    h->rx_last_pos = 0u;
    h->last_tx_len = 0u;
    h->tx_busy = 0u;
    return RET_OK;
}

ret_code_t hal_uart_port_rx_start(hal_uart_port_handle_t* h) {
    REQUIRE_RET(h != NULL, UART_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG));
    REQUIRE_RET(h->initialized, UART_RET(RET_CLASS_STATE, RET_R_NOT_READY));

#if (defined(CFG_PARAM_UART_RX_USE_DMA_IDLE) && (CFG_PARAM_UART_RX_USE_DMA_IDLE == 1))
    if (HAL_UARTEx_ReceiveToIdle_DMA(h->bsp.huart, h->bsp.rx_dma_buf,
                                     (uint16_t)h->bsp.rx_dma_len) != HAL_OK) {
        return UART_RET(RET_CLASS_IO, RET_R_IO);
    }
#if (defined(CFG_PARAM_UART_DISABLE_DMA_IT_HT) && (CFG_PARAM_UART_DISABLE_DMA_IT_HT == 1))
    __HAL_DMA_DISABLE_IT(h->bsp.huart->hdmarx, DMA_IT_HT);
#endif
#else
    if (HAL_UART_Receive_DMA(h->bsp.huart, h->bsp.rx_dma_buf, (uint16_t)h->bsp.rx_dma_len) !=
        HAL_OK) {
        return UART_RET(RET_CLASS_IO, RET_R_IO);
    }
#if defined(UART_IT_IDLE)
    __HAL_UART_ENABLE_IT(h->bsp.huart, UART_IT_IDLE);
#endif
#endif

    h->rx_last_pos = uart_dma_pos(h);
    return RET_OK;
}

ret_code_t hal_uart_port_send_async(hal_uart_port_handle_t* h, const uint8_t* buf, uint32_t len) {
    osal_crit_state_t cs = 0u;

    REQUIRE_RET((h != NULL) && (buf != NULL) && (len != 0u),
                UART_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG));
    REQUIRE_RET(h->initialized, UART_RET(RET_CLASS_STATE, RET_R_NOT_READY));
    REQUIRE_RET(len <= (uint32_t)UINT16_MAX, UART_RET(RET_CLASS_PARAM, RET_R_RANGE_ERR));

    OSAL_enter_critical_ex(&cs);
    if (h->tx_busy != 0u) {
        OSAL_exit_critical_ex(cs);
        return UART_RET(RET_CLASS_STATE, RET_R_BUSY);
    }
    h->tx_busy = 1u;
    h->last_tx_len = len;
    OSAL_exit_critical_ex(cs);

    stm32_uart_dma_tx_clean(buf, len);
    if (HAL_UART_Transmit_DMA(h->bsp.huart, (uint8_t*)buf, (uint16_t)len) != HAL_OK) {
        (void)uart_take_tx_len_and_clear_busy(h);
        return UART_RET(RET_CLASS_IO, RET_R_IO);
    }
    return RET_OK;
}

ret_code_t hal_uart_port_set_evt_cb(hal_uart_port_handle_t* h, hal_uart_port_evt_cb_t cb,
                                    void* user) {
    REQUIRE_RET(h != NULL, UART_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG));
    REQUIRE_RET(h->initialized, UART_RET(RET_CLASS_STATE, RET_R_NOT_READY));

    h->evt_cb = cb;
    h->evt_user = user;
    return RET_OK;
}

hal_uart_id_t hal_uart_port_get_id(const hal_uart_port_handle_t* h) {
    return (h != NULL) ? h->id : HAL_UART_ID_MAX;
}

uint32_t hal_uart_port_get_rx_buffer_len(const hal_uart_port_handle_t* h) {
    return (h != NULL) ? h->bsp.hal_rx_buffer_len : 0u;
}

#endif
