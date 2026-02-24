#include "APP_config.h"
#if defined(CFG_TARGET_PLATFORM_STM32_HAL) && defined(CFG_FEAT_HAL_SPI) && (CFG_FEAT_HAL_SPI == 1)

#include <limits.h>
#include <string.h>

#include "hal_spi_port.h"
#include "osal.h"
#include "assert_cus.h"
#include "stm32_spi_series.h"

/* 状态码 */
#define SPI_PORT_PARAM(reason_)   RET_MAKE_PARAM(RET_MOD_PORT, RET_SUB_PORT_SPI, (reason_))
#define SPI_PORT_STATE(reason_)   RET_MAKE_STATE(RET_MOD_PORT, RET_SUB_PORT_SPI, (reason_))
#define SPI_PORT_TIMEOUT(reason_) RET_MAKE_TIMEOUT(RET_MOD_PORT, RET_SUB_PORT_SPI, (reason_))
#define SPI_PORT_IO(reason_)      RET_MAKE_IO(RET_MOD_PORT, RET_SUB_PORT_SPI, (reason_))
#define SPI_PORT_RES(reason_)     RET_MAKE_RESOURCE(RET_MOD_PORT, RET_SUB_PORT_SPI, (reason_))

/* 打开的 SPI 端口注册表：用于在 HAL 回调里反查 ctx */
static hal_spi_port_ctx_t *s_spi_ctxs[HAL_SPI_BUS_MAX];

__WEAK ret_code_t stm32_spi_bsp_get(uint8_t bus_id, stm32_spi_bsp_t *out) {
    (void)bus_id;
    (void)out;
    /* 默认not_ready */
    return RET_MAKE_STATE(RET_MOD_PORT, RET_SUB_PORT_SPI, RET_R_NOT_READY);
}

__WEAK uint32_t stm32_spi_busclk_hz(const SPI_HandleTypeDef *hspi) {
    (void)hspi;
    return 48000000u;
}

__WEAK void stm32_spi_dma_tx_clean(const void *ptr, uint32_t len) {
    (void)ptr;
    (void)len;
}

__WEAK void stm32_spi_dma_rx_invalidate(const void *ptr, uint32_t len) {
    (void)ptr;
    (void)len;
}

/**
 * @brief 安全的依据目标设备而返回安全的分频系数
 * @param src_hz 总线频率
 * @param target_hz 目标设备需要的频率
 * @return 预分频系数
 */
static uint32_t pick_prescaler(uint32_t src_hz, uint32_t target_hz) {
    if (target_hz == 0u) return SPI_BAUDRATEPRESCALER_256;
    const uint32_t div = (src_hz + target_hz - 1u) / target_hz;

    if (div <= 2u) return SPI_BAUDRATEPRESCALER_2;
    if (div <= 4u) return SPI_BAUDRATEPRESCALER_4;
    if (div <= 8u) return SPI_BAUDRATEPRESCALER_8;
    if (div <= 16u) return SPI_BAUDRATEPRESCALER_16;
    if (div <= 32u) return SPI_BAUDRATEPRESCALER_32;
    if (div <= 64u) return SPI_BAUDRATEPRESCALER_64;
    if (div <= 128u) return SPI_BAUDRATEPRESCALER_128;
    return SPI_BAUDRATEPRESCALER_256;
}
/**
 * @brief 把自定义的枚举映射为底层配置需要的时钟相位、极性
 * @param m 枚举模式
 * @param cpol 极性
 * @param cpha 相位
 */
static void map_mode(hal_spi_mode_t m, uint32_t *cpol, uint32_t *cpha) {
    switch (m) {
        case HAL_SPI_MODE0:
            *cpol = SPI_POLARITY_LOW;
            *cpha = SPI_PHASE_1EDGE;
            break;
        case HAL_SPI_MODE1:
            *cpol = SPI_POLARITY_LOW;
            *cpha = SPI_PHASE_2EDGE;
            break;
        case HAL_SPI_MODE2:
            *cpol = SPI_POLARITY_HIGH;
            *cpha = SPI_PHASE_1EDGE;
            break;
        default:
            *cpol = SPI_POLARITY_HIGH;
            *cpha = SPI_PHASE_2EDGE;
            break;
    }
}
/**
 * @brief 查找空闲注册槽位
 * @return 槽位索引，不存在返回 -1
 */
static int32_t find_free_slot(void) {
    for (int32_t i = 0; i < (int32_t)HAL_SPI_BUS_MAX; i++) {
        if (s_spi_ctxs[i] == NULL) return i;
    }
    return -1;
}
/**
 * @brief 根据 HAL 句柄反查端口上下文
 * @param hspi SPI HAL 句柄
 * @return 端口上下文
 */
static hal_spi_port_ctx_t *find_ctx_by_hspi(const SPI_HandleTypeDef *hspi) {
    if (!hspi) return NULL;
    for (uint32_t i = 0; i < HAL_SPI_BUS_MAX; i++) {
        if (s_spi_ctxs[i] && s_spi_ctxs[i]->opened && s_spi_ctxs[i]->bsp.hspi == hspi) {
            return s_spi_ctxs[i];
        }
    }
    return NULL;
}
/**
 * @brief 清理异步传输上下文
 * @param ctx port 句柄
 */
static inline void clear_xfer_ctx(hal_spi_port_ctx_t *ctx) {
    CORE_ASSERT(ctx != NULL);
    if (!ctx) return;
    ctx->xfer_busy        = 0u;
    ctx->req_type         = HAL_SPI_PORT_REQ_NONE;
    ctx->active_len       = 0u;
    ctx->active_tx        = NULL;
    ctx->active_rx        = NULL;
    ctx->hw_stream_active = false;
}
/**
 * @brief 统一把 HAL 状态映射为 port 错误码
 * @param st HAL 状态
 * @return port 错误码
 */
static ret_code_t map_hal_status(HAL_StatusTypeDef st) {
    if (st == HAL_OK) return RET_OK;
    if (st == HAL_TIMEOUT) return SPI_PORT_TIMEOUT(RET_R_TIMEOUT);
    if (st == HAL_BUSY) return SPI_PORT_STATE(RET_R_BUSY);
    return SPI_PORT_IO(RET_R_HW_FAULT);
}
/**
 * @brief 把当前的rx tx地址转化为port层使用的通信方向
 * @param xfer 事务句柄
 * @return 通信方向
 */
static inline hal_spi_port_req_t resolve_req_type(const hal_spi_xfer_t *xfer) {
    if (!xfer) return HAL_SPI_PORT_REQ_NONE;
    if (xfer->tx && xfer->rx) return HAL_SPI_PORT_REQ_TXRX;
    if (xfer->tx) return HAL_SPI_PORT_REQ_TX;
    if (xfer->rx) return HAL_SPI_PORT_REQ_RX;
    return HAL_SPI_PORT_REQ_NONE;
}

static bool is_dma_circular_cfg(const DMA_HandleTypeDef *hdma) {
    if (!hdma) return false;
#if defined(DMA_CIRCULAR)
    return (hdma->Init.Mode == DMA_CIRCULAR);
#else
    return false;
#endif
}
/**
 * @brief 检查当前的配置 和 DMA配置是否相符
 * @param ctx port层SPI句柄
 * @param xfer 事务配置
 * @param hspi 底层SPI句柄
 * @param req_type 当前事务类型
 * @return
 */
static ret_code_t validate_xfer_runtime(const hal_spi_port_ctx_t *ctx, const hal_spi_xfer_t *xfer,
                                        const SPI_HandleTypeDef *hspi,
                                        hal_spi_port_req_t req_type) {
    REQUIRE_RET((ctx != NULL) && (xfer != NULL) && (hspi != NULL), SPI_PORT_PARAM(RET_R_NULL_PTR));

    const uint32_t mode = hspi->Init.Mode;
    const uint32_t dir  = hspi->Init.Direction;
    /* ============= 模式组合合法性 检查 =========== */
    /* 发送接收地址不能都为空 */
    REQUIRE_RET(req_type != HAL_SPI_PORT_REQ_NONE, SPI_PORT_PARAM(RET_R_INVALID_ARG));

    /* 单线/双线仅接收模式不支持 TxRx API 组合。 */
    REQUIRE_RET((req_type != HAL_SPI_PORT_REQ_TXRX) || (dir == SPI_DIRECTION_2LINES),
                SPI_PORT_PARAM(RET_R_UNSUPPORTED));
    /* 仅接收方向禁止仅发送。 */
    REQUIRE_RET((req_type != HAL_SPI_PORT_REQ_TX) || (dir != SPI_DIRECTION_2LINES_RXONLY),
                SPI_PORT_PARAM(RET_R_UNSUPPORTED));

    /* DMA 检查 */
    if (ctx->use_dma) {
        /* 必须初始化对应的 DMA通道 */
        switch (req_type) {
            case HAL_SPI_PORT_REQ_TXRX:
                if ((ctx->bsp.hdma_tx == NULL) || (ctx->bsp.hdma_rx == NULL))
                    return SPI_PORT_STATE(RET_R_NOT_READY);
                break;
            case HAL_SPI_PORT_REQ_TX:
                if (ctx->bsp.hdma_tx == NULL) return SPI_PORT_STATE(RET_R_NOT_READY);
                break;
            case HAL_SPI_PORT_REQ_RX:
                if (ctx->bsp.hdma_rx == NULL) return SPI_PORT_STATE(RET_R_NOT_READY);
                /*
                 * F4 HAL 在 Master+2LINES+RxOnly 下会走 TxRx DMA 以产生时钟，
                 * 因此这里要求同时具备 TX DMA，避免运行期断言失败。
                 */
                if ((mode == SPI_MODE_MASTER) && (dir == SPI_DIRECTION_2LINES) &&
                    !ctx->bsp.hdma_tx) {
                    return SPI_PORT_STATE(RET_R_NOT_READY);
                }
                break;
            default:
                return SPI_PORT_PARAM(RET_R_INVALID_ARG);
        }
    }

#if defined(CFG_PARAM_SPI_STRICT_XFER_CHECK) && (CFG_PARAM_SPI_STRICT_XFER_CHECK == 1)
    /* 严格模式：补充策略约束 补充检查当前的是不是循环模式 */
    const bool is_stream   = (xfer->flags & HAL_SPI_XFER_STREAM) != 0u;
    const bool stream_last = (xfer->flags & HAL_SPI_XFER_STREAM_END) != 0u;
    const bool hw_stream   = (xfer->flags & HAL_SPI_XFER_HW_STREAM) != 0u;

    REQUIRE_RET(!stream_last || is_stream, SPI_PORT_PARAM(RET_R_INVALID_ARG));
    if (hw_stream && !ctx->use_dma) return SPI_PORT_PARAM(RET_R_UNSUPPORTED);

    if (hw_stream && ctx->use_dma) {
        switch (req_type) {
            case HAL_SPI_PORT_REQ_TXRX:
                if (!is_dma_circular_cfg(ctx->bsp.hdma_tx) ||
                    !is_dma_circular_cfg(ctx->bsp.hdma_rx))
                    return SPI_PORT_PARAM(RET_R_UNSUPPORTED);
                break;
            case HAL_SPI_PORT_REQ_TX:
                if (!is_dma_circular_cfg(ctx->bsp.hdma_tx))
                    return SPI_PORT_PARAM(RET_R_UNSUPPORTED);
                break;
            case HAL_SPI_PORT_REQ_RX:
                if (!is_dma_circular_cfg(ctx->bsp.hdma_rx))
                    return SPI_PORT_PARAM(RET_R_UNSUPPORTED);
                if ((mode == SPI_MODE_MASTER) && (dir == SPI_DIRECTION_2LINES) &&
                    !is_dma_circular_cfg(ctx->bsp.hdma_tx)) {
                    return SPI_PORT_PARAM(RET_R_UNSUPPORTED);
                }
                break;
            default:
                return SPI_PORT_PARAM(RET_R_INVALID_ARG);
        }
    }

    /* Master 单线/双线仅接收不支持流模式（软/硬） */
    if ((is_stream || hw_stream) && (mode == SPI_MODE_MASTER) &&
        ((dir == SPI_DIRECTION_1LINE) || (dir == SPI_DIRECTION_2LINES_RXONLY))) {
        return SPI_PORT_PARAM(RET_R_UNSUPPORTED);
    }
#else
    (void)xfer;
#endif
    return RET_OK;
}
/**
 * @brief 根据传入的字节大小和 位宽计算出底层真正的传输 len
 * @param h 底层SPI句柄
 * @param xfer 事务句柄
 * @param tx  数据接收地址
 * @param rx  数据源地址
 * @param out_frame_count 返回实际给底层api 的 len长度
 * @return 32位状态码
 */
static ret_code_t calc_xfer_frame_meta(const SPI_HandleTypeDef *h, const hal_spi_xfer_t *xfer,
                                       const uint8_t *tx, uint8_t *rx, uint16_t *out_frame_count) {
    REQUIRE_RET((h != NULL) && (xfer != NULL) && (out_frame_count != NULL),
                SPI_PORT_PARAM(RET_R_NULL_PTR));

    uint32_t frame_bytes = 1u;
#if defined(SPI_DATASIZE_16BIT)
    if (h->Init.DataSize == SPI_DATASIZE_16BIT) frame_bytes = 2u;
#endif
    REQUIRE_RET((xfer->len % frame_bytes) == 0u, SPI_PORT_PARAM(RET_R_RANGE_ERR));

    const uint32_t frame_count = xfer->len / frame_bytes;
    REQUIRE_RET((frame_count != 0u) && (frame_count <= (uint32_t)UINT16_MAX),
                SPI_PORT_PARAM(RET_R_RANGE_ERR));

    /* 16bit 帧时要求地址按 half-word 对齐，避免 DMA/外设访问异常 */
    if (frame_bytes == 2u) {
        if ((tx && (((uintptr_t)tx & 0x1u) != 0u)) || (rx && (((uintptr_t)rx & 0x1u) != 0u))) {
            return SPI_PORT_PARAM(RET_R_INVALID_ARG);
        }
    }

    *out_frame_count = (uint16_t)frame_count;
    return RET_OK;
}

static inline void spi_dma_prepare_tx_buf(const uint8_t *tx, uint32_t len) {
    if (tx && len) stm32_spi_dma_tx_clean(tx, len);
}

static inline void spi_dma_prepare_rx_buf(uint8_t *rx, uint32_t len) {
    if (rx && len) stm32_spi_dma_rx_invalidate(rx, len);
}
/**
 * @brief 按 IRQn 分发到已注册的 SPI HAL IRQ 处理函数
 * @param irqn 当前中断号
 */
static void dispatch_spi_irq(IRQn_Type irqn) {
    for (uint32_t i = 0; i < HAL_SPI_BUS_MAX; i++) {
        hal_spi_port_ctx_t *ctx = s_spi_ctxs[i];
        /* 必须是已经注册且端口已经注册 且 拿到了句柄 */
        if (!ctx || !ctx->opened || !ctx->use_irq) continue;
        if (ctx->bsp.spi_irq == irqn && ctx->bsp.hspi) {
            HAL_SPI_IRQHandler(ctx->bsp.hspi);
            return;
        }
    }
}
/**
 * @brief 分发异步完成回调
 * @param ctx 端口句柄
 * @param rc port 语义返回码
 * @param bytes 完成字节数
 */
static void emit_port_evt(hal_spi_port_ctx_t *ctx, hal_spi_port_evt_type_t type, ret_code_t rc,
                          uint32_t bytes, bool clear_ctx) {
    if (!ctx) return;
    const hal_spi_port_evt_t evt = {
        .type    = type,
        .rc_port = rc,
        .bytes   = bytes,
    };
    if (clear_ctx) clear_xfer_ctx(ctx);
    if (ctx->evt_cb) ctx->evt_cb(ctx->evt_user, &evt);
}
/**
 * @brief 获取板级的句柄并完成总线能力初始化
 * @param cfg 总线配置给 port层缓存一份dma、irq的配置用于 port层配置的合法判断
 * @param out 返回底层的SPI句柄
 * @return 32位状态码
 * @note DMA / IRQ 开关由 cfg 控制
 */
ret_code_t hal_spi_port_open(const hal_spi_bus_cfg_t *cfg, hal_spi_port_ctx_t *out) {
    REQUIRE_RET((cfg != NULL) && (out != NULL), SPI_PORT_PARAM(RET_R_NULL_PTR));
    memset(out, 0, sizeof(*out));

    /* 至少选择一种异步能力 */
    if (!cfg->use_dma && !cfg->use_irq) return SPI_PORT_PARAM(RET_R_UNSUPPORTED);

    /* 根据板级id 让bsp填充 资源池中的对象的配置SPI句柄、DMA句柄 、中断、中断优先级 */
    const ret_code_t rc = stm32_spi_bsp_get(cfg->bus_id, &out->bsp);
    if (ret_is_err(rc)) return rc;
    if (!out->bsp.hspi) return SPI_PORT_STATE(RET_R_NOT_READY);

    out->use_dma = cfg->use_dma;
    out->use_irq = cfg->use_irq;

    /* 若启用 DMA，仅做句柄关联（DMA init 由外部 BSP/Cube 完成） */
    if (out->use_dma) {
        REQUIRE_RET((out->bsp.hdma_tx != NULL) || (out->bsp.hdma_rx != NULL),
                    SPI_PORT_STATE(RET_R_NOT_READY));
        if (out->bsp.hdma_tx && !out->bsp.hspi->hdmatx) {
            out->bsp.hspi->hdmatx    = out->bsp.hdma_tx;
            out->bsp.hdma_tx->Parent = out->bsp.hspi;
        }
        if (out->bsp.hdma_rx && !out->bsp.hspi->hdmarx) {
            out->bsp.hspi->hdmarx    = out->bsp.hdma_rx;
            out->bsp.hdma_rx->Parent = out->bsp.hspi;
        }
    }

    /* 若启用 SPI 中断，在端口层完成 NVIC 配置 */
    if (out->use_irq) {
        if ((int32_t)out->bsp.spi_irq < 0) return SPI_PORT_STATE(RET_R_NOT_READY);
        HAL_NVIC_SetPriority(out->bsp.spi_irq, out->bsp.irq_prio, out->bsp.irq_sub_prio);
        HAL_NVIC_EnableIRQ(out->bsp.spi_irq);
    }
    /* 注册port（临界区保护，避免并发 open 抢同一槽位） */
    int32_t slot         = -1;
    osal_crit_state_t cs = 0u;
    OSAL_enter_critical_ex(&cs);
    slot = find_free_slot();
    if (slot >= 0) s_spi_ctxs[slot] = out;
    OSAL_exit_critical_ex(cs);
    if (slot < 0) return SPI_PORT_RES(RET_R_NO_RESOURCE);
    out->opened = true;
    return RET_OK;
}
/**
 * @brief 关闭端口占用的底层资源并复位上下文
 * @param ctx 底层port上下文
 * @return 32位状态码
 */
ret_code_t hal_spi_port_close(hal_spi_port_ctx_t *ctx) {
    /* 参数检查 */
    REQUIRE_RET(ctx != NULL, SPI_PORT_PARAM(RET_R_NULL_PTR));
    if (!ctx->opened) return SPI_PORT_STATE(RET_R_NOT_READY);
    if (ctx->xfer_busy) return SPI_PORT_STATE(RET_R_BUSY);

    osal_crit_state_t cs = 0u;
    OSAL_enter_critical_ex(&cs);
    for (uint32_t i = 0; i < HAL_SPI_BUS_MAX; i++) {
        if (s_spi_ctxs[i] == ctx) {
            s_spi_ctxs[i] = NULL;
            break;
        }
    }
    OSAL_exit_critical_ex(cs);
    /* 关闭DMA 中断*/
    if (ctx->use_irq && (int32_t)ctx->bsp.spi_irq >= 0) {
        HAL_NVIC_DisableIRQ(ctx->bsp.spi_irq);
    }
    /* 关闭SPI 句柄初始化 */
    if (ctx->bsp.hspi) {
        (void)HAL_SPI_DeInit(ctx->bsp.hspi);
    }
    memset(ctx, 0, sizeof(*ctx));
    return RET_OK;
}
/**
 * @brief 注册 port 层异步完成回调
 * @param ctx port 句柄
 * @param cb 完成回调
 * @param user 用户上下文
 * @return 32位状态码
 */
ret_code_t hal_spi_port_set_evt_cb(hal_spi_port_ctx_t *ctx, hal_spi_port_evt_cb_t cb, void *user) {
    REQUIRE_RET(ctx != NULL, SPI_PORT_PARAM(RET_R_NULL_PTR));
    ctx->evt_cb   = cb;
    ctx->evt_user = user;
    return RET_OK;
}
/**
 *
 * @param ctx 板级SPI句柄
 * @param dev_cfg 设备的配置
 * @param bus_default_hz 默认频率
 * @return
 */
ret_code_t hal_spi_port_apply(hal_spi_port_ctx_t *ctx, const hal_spi_dev_cfg_t *dev_cfg,
                              uint32_t bus_default_hz) {
    /* 参数检查 */
    REQUIRE_RET((ctx != NULL) && (dev_cfg != NULL), SPI_PORT_PARAM(RET_R_NULL_PTR));
    if (!ctx->opened) return SPI_PORT_STATE(RET_R_NOT_READY);
    /* 不能在已有事务进行的情况下 重新配置 */
    if (ctx->xfer_busy) return SPI_PORT_STATE(RET_R_BUSY);

    /* 取出句柄的地址 */
    SPI_HandleTypeDef *h = ctx->bsp.hspi;
    if (!h) return SPI_PORT_STATE(RET_R_NOT_READY);
    /* 通信频率 <= default_hz */
    const uint32_t hz = (dev_cfg->max_hz < bus_default_hz) ? dev_cfg->max_hz : bus_default_hz;
    /* 参数没变就不初始化了 */
    if (ctx->cfg_cache_valid && ctx->cur_mode == dev_cfg->mode &&
        ctx->cur_order == dev_cfg->bit_order && ctx->cur_bits == dev_cfg->frame_bits &&
        ctx->cur_hz == hz) {
        return RET_OK;
    }
    /* 时钟相位、和极性 */
    uint32_t cpol = 0u, cpha = 0u;
    map_mode(dev_cfg->mode, &cpol, &cpha);

    /* 按需重配 */
    (void)HAL_SPI_DeInit(h);

    h->Init.Mode        = dev_cfg->is_master ? SPI_MODE_MASTER : SPI_MODE_SLAVE;
    h->Init.Direction   = dev_cfg->dir == HAL_SPI_DIR_LINE            ? SPI_DIRECTION_1LINE
                          : dev_cfg->dir == HAL_SPI_DIR_2LINES_RXONLY ? SPI_DIRECTION_2LINES_RXONLY
                                                                      : SPI_DIRECTION_2LINES;
    h->Init.CLKPolarity = cpol;
    h->Init.CLKPhase    = cpha;
    /* NSS 选择：软件片选固定 SOFT；硬件片选按主从选择输出/输入 */
    if (dev_cfg->cs_type == HAL_SPI_CS_GPIO) {
        h->Init.NSS = SPI_NSS_SOFT;
    } else {
        h->Init.NSS = dev_cfg->is_master ? SPI_NSS_HARD_OUTPUT : SPI_NSS_HARD_INPUT;
    }
    h->Init.FirstBit =
        (dev_cfg->bit_order == HAL_SPI_BITORDER_LSB) ? SPI_FIRSTBIT_LSB : SPI_FIRSTBIT_MSB;
    h->Init.TIMode =
        dev_cfg->use_ti_mode ? SPI_TIMODE_ENABLE : SPI_TIMODE_DISABLE; /* 德州仪器模式 */
    h->Init.CRCCalculation =
        dev_cfg->use_crc ? SPI_CRCCALCULATION_ENABLE : SPI_CRCCALCULATION_DISABLE; /* CRC计算 */

#if defined(SPI_DATASIZE_16BIT)
    h->Init.DataSize =
        (dev_cfg->frame_bits == HAL_SPI_FRAME_16) ? SPI_DATASIZE_16BIT : SPI_DATASIZE_8BIT;
#else
    /* 老 HAL 版本不支持可变 datasize：直接拒绝 16bit */
    if (dev_cfg->frame_bits == HAL_SPI_FRAME_16) return SPI_PORT_PARAM(RET_R_UNSUPPORTED);
    h->Init.DataSize = SPI_DATASIZE_8BIT;
#endif
    /* 返回当前句柄挂载的总线频率 */
    const uint32_t src        = stm32_spi_busclk_hz(h);
    /* 根据总线频率和目标频率预分频 */
    h->Init.BaudRatePrescaler = pick_prescaler(src, hz);

    if (HAL_SPI_Init(h) != HAL_OK) return SPI_PORT_IO(RET_R_HW_FAULT);
    /* 当前配置判定生效 */
    ctx->cfg_cache_valid = true;
    ctx->cur_mode        = dev_cfg->mode;
    ctx->cur_order       = dev_cfg->bit_order;
    ctx->cur_bits        = dev_cfg->frame_bits;
    ctx->cur_hz          = hz;
    return RET_OK;
}
/**
 * @brief
 * @param ctx
 * @param xfer
 * @return
 */
ret_code_t hal_spi_port_stream_start(hal_spi_port_ctx_t *ctx, const hal_spi_xfer_t *xfer) {
    /* 参数检查 */
    REQUIRE_RET((ctx != NULL) && (xfer != NULL), SPI_PORT_PARAM(RET_R_NULL_PTR));
    if (!ctx->opened) return SPI_PORT_STATE(RET_R_NOT_READY);
    if (ctx->xfer_busy) return SPI_PORT_STATE(RET_R_BUSY);
    if (!ctx->use_dma) return SPI_PORT_PARAM(RET_R_UNSUPPORTED);

    SPI_HandleTypeDef *h = ctx->bsp.hspi;
    /* 必须已经注册 */
    if (!h) return SPI_PORT_STATE(RET_R_NOT_READY);
    REQUIRE_RET(xfer->len != 0u, SPI_PORT_PARAM(RET_R_RANGE_ERR));
    /* 获取 事务 */
    hal_spi_xfer_t stream_xfer = *xfer;
    /* 添加上 硬件DMA 流事务 */
    stream_xfer.flags |= HAL_SPI_XFER_HW_STREAM;
    /* 判断当前是什么方向的传输 */
    const hal_spi_port_req_t req_type = resolve_req_type(&stream_xfer);
    /* 检查当前的传输方向、DMA、配置是否是合法的组合 */
    const ret_code_t vrc              = validate_xfer_runtime(ctx, &stream_xfer, h, req_type);
    if (ret_is_err(vrc)) return vrc;

    const uint8_t *tx = (const uint8_t *)stream_xfer.tx;
    uint8_t *rx       = (uint8_t *)stream_xfer.rx;
    uint16_t frame_count;
    /* 计算真正的 传输len */
    const ret_code_t frc = calc_xfer_frame_meta(h, &stream_xfer, tx, rx, &frame_count);
    if (ret_is_err(frc)) return frc;
    /* 失效 和 清理缓存 */
    spi_dma_prepare_tx_buf(tx, stream_xfer.len);
    spi_dma_prepare_rx_buf(rx, stream_xfer.len);

    HAL_StatusTypeDef st  = HAL_ERROR;
    ctx->xfer_busy        = 1u;
    ctx->req_type         = req_type;
    ctx->active_len       = stream_xfer.len;
    ctx->active_tx        = tx;
    ctx->active_rx        = rx;
    ctx->hw_stream_active = true;
    /* 发送、接收数据 */
    switch (req_type) {
        case HAL_SPI_PORT_REQ_TXRX:
            st = HAL_SPI_TransmitReceive_DMA(h, (uint8_t *)tx, rx, (uint16_t)frame_count);
            break;
        case HAL_SPI_PORT_REQ_TX:
            st = HAL_SPI_Transmit_DMA(h, (uint8_t *)tx, (uint16_t)frame_count);
            break;
        case HAL_SPI_PORT_REQ_RX:
            st = HAL_SPI_Receive_DMA(h, rx, (uint16_t)frame_count);
            break;
        default:
            clear_xfer_ctx(ctx);
            return SPI_PORT_PARAM(RET_R_INVALID_ARG);
    }

    if (st != HAL_OK) {
        clear_xfer_ctx(ctx);
        return map_hal_status(st);
    }
    return RET_OK;
}
/**
 * @brief 停止DMA 根据配置决定DeInit SPI
 * @param ctx port 句柄
 * @param disable_spi 是否 DeInit SPI
 * @return
 */
ret_code_t hal_spi_port_stream_stop(hal_spi_port_ctx_t *ctx, bool disable_spi) {
    /* 参数检查 */
    REQUIRE_RET(ctx != NULL, SPI_PORT_PARAM(RET_R_NULL_PTR));
    if (!ctx->opened) return SPI_PORT_STATE(RET_R_NOT_READY);
    if (!ctx->hw_stream_active) return SPI_PORT_STATE(RET_R_NOT_READY);
    if (ctx->bsp.hspi == NULL) return SPI_PORT_STATE(RET_R_NOT_READY);
    /* 停止DMA  */
    if (HAL_SPI_DMAStop(ctx->bsp.hspi) != HAL_OK) {
        return SPI_PORT_IO(RET_R_HW_FAULT);
    }
    /* DeInit DMA */
    if (disable_spi) {
        if (HAL_SPI_DeInit(ctx->bsp.hspi) != HAL_OK) {
            return SPI_PORT_IO(RET_R_HW_FAULT);
        }
        ctx->cfg_cache_valid = false;
    }
    /* 清理事务 */
    clear_xfer_ctx(ctx);
    return RET_OK;
}
/**
 * @brief 发起一次异步 SPI 传输（发起后立即返回）
 * @param ctx 底层SPI句柄
 * @param xfer 每次传输的配置信息 决定当前是全双工半双工
 * @return
 */
ret_code_t hal_spi_port_xfer(hal_spi_port_ctx_t *ctx, const hal_spi_xfer_t *xfer) {
    /* 参数检查 */
    REQUIRE_RET((ctx != NULL) && (xfer != NULL), SPI_PORT_PARAM(RET_R_NULL_PTR));
    if (!ctx->opened) return SPI_PORT_STATE(RET_R_NOT_READY);
    if (ctx->xfer_busy) return SPI_PORT_STATE(RET_R_BUSY);

    SPI_HandleTypeDef *h = ctx->bsp.hspi;
    if (!h) return SPI_PORT_STATE(RET_R_NOT_READY);

    REQUIRE_RET(xfer->len != 0u, SPI_PORT_PARAM(RET_R_RANGE_ERR));
    if ((xfer->flags & HAL_SPI_XFER_HW_STREAM) != 0u) return SPI_PORT_PARAM(RET_R_UNSUPPORTED);
    if (!ctx->use_dma && !ctx->use_irq) return SPI_PORT_PARAM(RET_R_UNSUPPORTED);
    /* 发送和接收的地址 */
    const uint8_t *tx                 = (const uint8_t *)xfer->tx;
    uint8_t *rx                       = (uint8_t *)xfer->rx;
    /* 判断方向 */
    const hal_spi_port_req_t req_type = resolve_req_type(xfer);
    /* 检查当前是否是有效的配置 DMA 和 hal配置 */
    const ret_code_t vrc              = validate_xfer_runtime(ctx, xfer, h, req_type);
    if (ret_is_err(vrc)) return vrc;

    uint16_t frame_count;
    /* 检查对齐 计算出真正的传输len */
    const ret_code_t frc = calc_xfer_frame_meta(h, xfer, tx, rx, &frame_count);
    if (ret_is_err(frc)) return frc;
    if (ctx->use_dma) {
        spi_dma_prepare_tx_buf(tx, xfer->len);
        spi_dma_prepare_rx_buf(rx, xfer->len);
    }

    HAL_StatusTypeDef st  = HAL_ERROR;
    ctx->xfer_busy        = 1u;
    ctx->req_type         = req_type;
    ctx->active_len       = xfer->len;
    ctx->active_tx        = tx;
    ctx->active_rx        = rx;
    ctx->hw_stream_active = false;
    /* 根据传输的方向和dma配置 完成对应的 API 调用 */
    switch (req_type) {
        case HAL_SPI_PORT_REQ_TXRX:
            if (ctx->use_dma) {
                st = HAL_SPI_TransmitReceive_DMA(h, (uint8_t *)tx, rx, (uint16_t)frame_count);
            } else if (ctx->use_irq) {
                st = HAL_SPI_TransmitReceive_IT(h, (uint8_t *)tx, rx, (uint16_t)frame_count);
            } else {
                clear_xfer_ctx(ctx);
                return SPI_PORT_PARAM(RET_R_UNSUPPORTED);
            }
            break;
        case HAL_SPI_PORT_REQ_TX:
            if (ctx->use_dma) {
                st = HAL_SPI_Transmit_DMA(h, (uint8_t *)tx, (uint16_t)frame_count);
            } else if (ctx->use_irq) {
                st = HAL_SPI_Transmit_IT(h, (uint8_t *)tx, (uint16_t)frame_count);
            } else {
                clear_xfer_ctx(ctx);
                return SPI_PORT_PARAM(RET_R_UNSUPPORTED);
            }
            break;
        case HAL_SPI_PORT_REQ_RX:
            if (ctx->use_dma) {
                st = HAL_SPI_Receive_DMA(h, rx, (uint16_t)frame_count);
            } else if (ctx->use_irq) {
                st = HAL_SPI_Receive_IT(h, rx, (uint16_t)frame_count);
            } else {
                clear_xfer_ctx(ctx);
                return SPI_PORT_PARAM(RET_R_UNSUPPORTED);
            }
            break;
        default:
            clear_xfer_ctx(ctx);
            return SPI_PORT_PARAM(RET_R_INVALID_ARG);
    }

    if (st != HAL_OK) {
        clear_xfer_ctx(ctx);
        return map_hal_status(st);
    }
    return RET_OK;
}

void hal_spi_port_irq_dispatch_hook(IRQn_Type irqn) {
    dispatch_spi_irq(irqn);
}
/**
 * @brief SPI 发送过半回调钩子
 * @param hspi 底层 SPI 句柄
 * @note 仅在 [仅发送 (TX) + 硬件流模式] 下触发，向上层抛出 HAL_SPI_PORT_EVT_STREAM_HALF 事件
 */
void hal_spi_port_tx_half_cplt_hook(SPI_HandleTypeDef *hspi) {
    hal_spi_port_ctx_t *ctx = find_ctx_by_hspi(hspi);
    if (!ctx || !ctx->xfer_busy || !ctx->hw_stream_active) return;
    if (ctx->req_type != HAL_SPI_PORT_REQ_TX) return;
    const uint32_t half_len         = ctx->active_len / 2u;
    const uint8_t *const second_ptr = (ctx->active_tx) ? (ctx->active_tx + half_len) : NULL;
    if (ctx->use_dma) spi_dma_prepare_tx_buf(second_ptr, ctx->active_len - half_len);
    emit_port_evt(ctx, HAL_SPI_PORT_EVT_STREAM_HALF, RET_OK, ctx->active_len / 2u, false);
}

/**
 * @brief SPI 接收过半回调钩子
 * @param hspi 底层 SPI 句柄
 * @note 仅在 [仅接收 (RX) + 硬件流模式] 下触发，向上层抛出 HAL_SPI_PORT_EVT_STREAM_HALF 事件
 */
void hal_spi_port_rx_half_cplt_hook(SPI_HandleTypeDef *hspi) {
    hal_spi_port_ctx_t *ctx = find_ctx_by_hspi(hspi);
    if (!ctx || !ctx->xfer_busy || !ctx->hw_stream_active) return;
    if (ctx->req_type != HAL_SPI_PORT_REQ_RX) return;
    const uint32_t half_len = ctx->active_len / 2u;
    if (ctx->use_dma) spi_dma_prepare_rx_buf(ctx->active_rx, half_len);
    emit_port_evt(ctx, HAL_SPI_PORT_EVT_STREAM_HALF, RET_OK, ctx->active_len / 2u, false);
}

/**
 * @brief SPI 发送完成回调钩子
 * @param hspi 底层 SPI 句柄
 * @note 适用于 [仅发送 (TX)] 模式。
 * - 硬件流模式：抛出 STREAM_FULL 事件（不释放上下文）
 * - 普通事务模式：抛出 DONE 事件（并释放上下文）
 */
void hal_spi_port_tx_cplt_hook(SPI_HandleTypeDef *hspi) {
    hal_spi_port_ctx_t *ctx = find_ctx_by_hspi(hspi);
    if (!ctx || !ctx->xfer_busy || (ctx->req_type != HAL_SPI_PORT_REQ_TX)) return;
    if (ctx->hw_stream_active) {
        const uint32_t half_len = ctx->active_len / 2u;
        if (ctx->use_dma) spi_dma_prepare_tx_buf(ctx->active_tx, half_len);
        emit_port_evt(ctx, HAL_SPI_PORT_EVT_STREAM_FULL, RET_OK, ctx->active_len, false);
        return;
    }
    emit_port_evt(ctx, HAL_SPI_PORT_EVT_DONE, RET_OK, ctx->active_len, true);
}

/**
 * @brief SPI 接收完成回调钩子
 * @param hspi 底层 SPI 句柄
 * @note 适用于 [仅接收 (RX)] 模式。
 * - 硬件流模式：抛出 STREAM_FULL 事件（不释放上下文）
 * - 普通事务模式：抛出 DONE 事件（并释放上下文）
 */
void hal_spi_port_rx_cplt_hook(SPI_HandleTypeDef *hspi) {
    hal_spi_port_ctx_t *ctx = find_ctx_by_hspi(hspi);
    if (!ctx || !ctx->xfer_busy || (ctx->req_type != HAL_SPI_PORT_REQ_RX)) return;
    if (ctx->hw_stream_active) {
        const uint32_t half_len   = ctx->active_len / 2u;
        uint8_t *const second_ptr = (ctx->active_rx) ? (ctx->active_rx + half_len) : NULL;
        if (ctx->use_dma) spi_dma_prepare_rx_buf(second_ptr, ctx->active_len - half_len);
        emit_port_evt(ctx, HAL_SPI_PORT_EVT_STREAM_FULL, RET_OK, ctx->active_len, false);
        return;
    }
    if (ctx->use_dma) spi_dma_prepare_rx_buf(ctx->active_rx, ctx->active_len);
    emit_port_evt(ctx, HAL_SPI_PORT_EVT_DONE, RET_OK, ctx->active_len, true);
}

/**
 * @brief SPI 全双工收发完成回调钩子
 * @param hspi 底层 SPI 句柄
 * @note 适用于 [全双工 (TXRX)] 模式。包含硬件流 (DMA Circular) 与单次事务。
 */
void hal_spi_port_txrx_cplt_hook(SPI_HandleTypeDef *hspi) {
    hal_spi_port_ctx_t *ctx = find_ctx_by_hspi(hspi);
    if (!ctx || !ctx->xfer_busy || (ctx->req_type != HAL_SPI_PORT_REQ_TXRX)) return;
    if (ctx->hw_stream_active) {
        const uint32_t half_len           = ctx->active_len / 2u;
        const uint8_t *const tx_first_ptr = ctx->active_tx;
        uint8_t *const second_ptr         = (ctx->active_rx) ? (ctx->active_rx + half_len) : NULL;
        if (ctx->use_dma) spi_dma_prepare_tx_buf(tx_first_ptr, half_len);
        if (ctx->use_dma) spi_dma_prepare_rx_buf(second_ptr, ctx->active_len - half_len);
        emit_port_evt(ctx, HAL_SPI_PORT_EVT_STREAM_FULL, RET_OK, ctx->active_len, false);
        return;
    }
    uint32_t bytes = ctx->active_len;
    if (hspi && hspi->hdmarx) {
        bytes = ctx->active_len - __HAL_DMA_GET_COUNTER(hspi->hdmarx);
    }
    if (ctx->use_dma) spi_dma_prepare_rx_buf(ctx->active_rx, bytes);
    emit_port_evt(ctx, HAL_SPI_PORT_EVT_DONE, RET_OK, bytes, true);
}

/**
 * @brief SPI 全双工收发过半回调钩子
 * @param hspi 底层 SPI 句柄
 * @note 仅在 [全双工 (TXRX) + 硬件流模式] 下触发，向上层抛出 HAL_SPI_PORT_EVT_STREAM_HALF 事件
 */
void hal_spi_port_txrx_half_cplt_hook(SPI_HandleTypeDef *hspi) {
    hal_spi_port_ctx_t *ctx = find_ctx_by_hspi(hspi);
    if (!ctx || !ctx->xfer_busy || !ctx->hw_stream_active) return;
    if (ctx->req_type != HAL_SPI_PORT_REQ_TXRX) return;
    const uint32_t half_len            = ctx->active_len / 2u;
    const uint8_t *const tx_second_ptr = (ctx->active_tx) ? (ctx->active_tx + half_len) : NULL;
    if (ctx->use_dma) spi_dma_prepare_tx_buf(tx_second_ptr, ctx->active_len - half_len);
    if (ctx->use_dma) spi_dma_prepare_rx_buf(ctx->active_rx, half_len);
    emit_port_evt(ctx, HAL_SPI_PORT_EVT_STREAM_HALF, RET_OK, ctx->active_len / 2u, false);
}

void hal_spi_port_error_hook(SPI_HandleTypeDef *hspi) {
    hal_spi_port_ctx_t *ctx = find_ctx_by_hspi(hspi);
    if (!ctx || !ctx->xfer_busy) return;
    emit_port_evt(ctx, HAL_SPI_PORT_EVT_ERROR, SPI_PORT_IO(RET_R_HW_FAULT), 0u, true);
}

#if defined(CFG_PARAM_SPI_PORT_USE_LOCAL_IRQ_HANDLER) && \
    (CFG_PARAM_SPI_PORT_USE_LOCAL_IRQ_HANDLER == 1)
#if defined(SPI1)
void SPI1_IRQHandler(void) {
    hal_spi_port_irq_dispatch_hook(SPI1_IRQn);
}
#endif
#if defined(SPI2)
void SPI2_IRQHandler(void) {
    hal_spi_port_irq_dispatch_hook(SPI2_IRQn);
}
#endif
#if defined(SPI3)
void SPI3_IRQHandler(void) {
    hal_spi_port_irq_dispatch_hook(SPI3_IRQn);
}
#endif
#if defined(SPI4)
void SPI4_IRQHandler(void) {
    hal_spi_port_irq_dispatch_hook(SPI4_IRQn);
}
#endif
#if defined(SPI5)
void SPI5_IRQHandler(void) {
    hal_spi_port_irq_dispatch_hook(SPI5_IRQn);
}
#endif
#if defined(SPI6)
void SPI6_IRQHandler(void) {
    hal_spi_port_irq_dispatch_hook(SPI6_IRQn);
}
#endif
#endif

#if defined(CFG_PARAM_SPI_PORT_USE_LOCAL_HAL_CALLBACKS) && \
    (CFG_PARAM_SPI_PORT_USE_LOCAL_HAL_CALLBACKS == 1)
/**
 * @brief SPI 发送完成回调（HAL 回调）
 * @param hspi SPI HAL 句柄
 */
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi) {
    hal_spi_port_tx_cplt_hook(hspi);
}
void HAL_SPI_TxHalfCpltCallback(SPI_HandleTypeDef *hspi) {
    hal_spi_port_tx_half_cplt_hook(hspi);
}
/**
 * @brief SPI 接收完成回调（HAL 回调）
 * @param hspi SPI HAL 句柄
 */
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi) {
    hal_spi_port_rx_cplt_hook(hspi);
}
void HAL_SPI_RxHalfCpltCallback(SPI_HandleTypeDef *hspi) {
    hal_spi_port_rx_half_cplt_hook(hspi);
}
/**
 * @brief SPI 全双工完成回调（HAL 回调）
 * @param hspi SPI HAL 句柄
 */
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi) {
    hal_spi_port_txrx_cplt_hook(hspi);
}
/**
 * @brief SPI 全双工完成回调（HAL 回调）
 * @param hspi SPI HAL 句柄
 */
void HAL_SPI_TxRxHalfCpltCallback(SPI_HandleTypeDef *hspi) {
    hal_spi_port_txrx_half_cplt_hook(hspi);
}
/**
 * @brief SPI 错误回调（HAL 回调）
 * @param hspi SPI HAL 句柄
 */
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi) {
    hal_spi_port_error_hook(hspi);
}
#endif

#endif
