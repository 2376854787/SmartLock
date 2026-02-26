#include "APP_config.h"
#if defined(CFG_TARGET_PLATFORM_STM32_HAL) && defined(CFG_FEAT_HAL_I2C) && (CFG_FEAT_HAL_I2C == 1)

#include <limits.h>
#include <string.h>

#include "assert_cus.h"
#include "hal_i2c_port.h"
#include "osal.h"
#include "stm32_i2c_series.h"

#define I2C_PORT_PARAM(reason_)   RET_MAKE_PARAM(RET_MOD_PORT, RET_SUB_PORT_I2C, (reason_))
#define I2C_PORT_STATE(reason_)   RET_MAKE_STATE(RET_MOD_PORT, RET_SUB_PORT_I2C, (reason_))
#define I2C_PORT_TIMEOUT(reason_) RET_MAKE_TIMEOUT(RET_MOD_PORT, RET_SUB_PORT_I2C, (reason_))
#define I2C_PORT_IO(reason_)      RET_MAKE_IO(RET_MOD_PORT, RET_SUB_PORT_I2C, (reason_))
#define I2C_PORT_RES(reason_)     RET_MAKE_RESOURCE(RET_MOD_PORT, RET_SUB_PORT_I2C, (reason_))

/* 打开的 I2C 端口注册表：用于在 HAL 回调里反查 ctx */
static hal_i2c_port_ctx_t *s_i2c_ctxs[HAL_I2C_BUS_MAX];

/**
 * @brief DMA TX DCache 清理弱钩子
 * @param ptr 缓冲区地址
 * @param len 缓冲区长度
 * @note F4 默认空实现；H7/F7 可在 series 层覆盖
 */
__WEAK void stm32_i2c_dma_tx_clean(const void *ptr, uint32_t len) {
    (void)ptr;
    (void)len;
}

/**
 * @brief DMA RX DCache 失效弱钩子
 * @param ptr 缓冲区地址
 * @param len 缓冲区长度
 * @note F4 默认空实现；H7/F7 可在 series 层覆盖
 */
__WEAK void stm32_i2c_dma_rx_invalidate(const void *ptr, uint32_t len) {
    (void)ptr;
    (void)len;
}

/**
 * @brief 将 HAL 抽象地址转换为 STM32 HAL API 使用格式
 * @param dev_cfg 设备配置
 * @return HAL API 地址值
 * @note 7bit 地址会左移一位；10bit 按原值传递
 */
static inline uint16_t i2c_to_hal_addr(const hal_i2c_dev_cfg_t *dev_cfg) {
    if (dev_cfg->addr_mode == HAL_I2C_ADDR_7BIT) {
        return (uint16_t)(dev_cfg->dev_addr << 1);
    }
    return dev_cfg->dev_addr;
}

/**
 * @brief 清理活跃事务上下文
 * @param ctx port 句柄
 */
static inline void clear_xfer_ctx(hal_i2c_port_ctx_t *ctx) {
    CORE_ASSERT(ctx != NULL);
    if (!ctx) return;
    ctx->xfer_busy      = 0u;
    ctx->active_dev_addr = 0u;
    ctx->active_tx_len  = 0u;
    ctx->active_rx_len  = 0u;
    ctx->active_tx      = NULL;
    ctx->active_rx      = NULL;
}

/**
 * @brief 查找空闲 ctx 注册槽位
 * @return 槽位索引，失败返回 -1
 */
static int32_t find_free_slot(void) {
    for (int32_t i = 0; i < (int32_t)HAL_I2C_BUS_MAX; i++) {
        if (s_i2c_ctxs[i] == NULL) return i;
    }
    return -1;
}

/**
 * @brief 由 HAL I2C 句柄反查 port 上下文
 * @param hi2c HAL I2C 句柄
 * @return 命中返回 ctx，失败返回 NULL
 */
static hal_i2c_port_ctx_t *find_ctx_by_hi2c(const I2C_HandleTypeDef *hi2c) {
    if (!hi2c) return NULL;
    for (uint32_t i = 0; i < HAL_I2C_BUS_MAX; i++) {
        if (s_i2c_ctxs[i] && s_i2c_ctxs[i]->opened && s_i2c_ctxs[i]->bsp.hi2c == hi2c) {
            return s_i2c_ctxs[i];
        }
    }
    return NULL;
}

/**
 * @brief HAL_StatusTypeDef 到 port 错误码映射
 * @param st HAL 状态
 * @return port 统一错误码
 */
static ret_code_t map_hal_status(HAL_StatusTypeDef st) {
    if (st == HAL_OK) return RET_OK;
    if (st == HAL_TIMEOUT) return I2C_PORT_TIMEOUT(RET_R_TIMEOUT);
    if (st == HAL_BUSY) return I2C_PORT_STATE(RET_R_BUSY);
    return I2C_PORT_IO(RET_R_HW_FAULT);
}

/**
 * @brief HAL I2C 错误位到 port 错误码映射
 * @param err hi2c->ErrorCode
 * @return port 统一错误码
 */
static ret_code_t map_hal_i2c_error(uint32_t err) {
#if defined(HAL_I2C_ERROR_TIMEOUT)
    if ((err & HAL_I2C_ERROR_TIMEOUT) != 0u) {
        return I2C_PORT_TIMEOUT(RET_R_TIMEOUT);
    }
#endif
#if defined(HAL_I2C_ERROR_AF)
    if ((err & HAL_I2C_ERROR_AF) != 0u) {
        return I2C_PORT_IO(RET_R_IO);
    }
#endif
#if defined(HAL_I2C_ERROR_ARLO)
    if ((err & HAL_I2C_ERROR_ARLO) != 0u) {
        return I2C_PORT_IO(RET_R_HW_FAULT);
    }
#endif
#if defined(HAL_I2C_ERROR_BERR)
    if ((err & HAL_I2C_ERROR_BERR) != 0u) {
        return I2C_PORT_IO(RET_R_HW_FAULT);
    }
#endif
    return I2C_PORT_IO(RET_R_HW_FAULT);
}

/**
 * @brief 向 HAL 层上报 port 事件
 * @param ctx                   port 句柄
 * @param type                  事件类型
 * @param rc                    port 错误码
 * @param tx_bytes              tx 字节数
 * @param rx_bytes              rx 字节数
 * @param clear_ctx_after_emit  上报后是否清理活跃上下文
 */
static void emit_port_evt(hal_i2c_port_ctx_t *ctx, hal_i2c_port_evt_type_t type, ret_code_t rc,
                          uint32_t tx_bytes, uint32_t rx_bytes, bool clear_ctx_after_emit) {
    if (!ctx) return;
    const hal_i2c_port_evt_t evt = {
        .type     = type,
        .rc_port  = rc,
        .tx_bytes = tx_bytes,
        .rx_bytes = rx_bytes,
    };
    if (clear_ctx_after_emit) clear_xfer_ctx(ctx);
    if (ctx->evt_cb) ctx->evt_cb(ctx->evt_user, &evt);
}

/**
 * @brief 运行时事务合法性检查
 * @param ctx port 句柄
 * @param dev 设备配置
 * @param xfer 事务参数
 * @return RET_OK 或错误码
 * @note 当前限制：
 * - 仅主机模式；
 * - tx/rx 不能同发；
 * - NO_STOP 未实现。
 */
static ret_code_t validate_xfer_runtime(const hal_i2c_port_ctx_t *ctx, const hal_i2c_dev_cfg_t *dev,
                                        const hal_i2c_xfer_t *xfer) {
    REQUIRE_RET((ctx != NULL) && (dev != NULL) && (xfer != NULL), I2C_PORT_PARAM(RET_R_NULL_PTR));

    if (!dev->is_master) return I2C_PORT_PARAM(RET_R_UNSUPPORTED);
    if ((xfer->tx_len == 0u) && (xfer->rx_len == 0u)) return I2C_PORT_PARAM(RET_R_RANGE_ERR);
    if ((xfer->tx_len > 0u) && (xfer->tx == NULL)) return I2C_PORT_PARAM(RET_R_NULL_PTR);
    if ((xfer->rx_len > 0u) && (xfer->rx == NULL)) return I2C_PORT_PARAM(RET_R_NULL_PTR);
    if ((xfer->tx_len > (uint32_t)UINT16_MAX) || (xfer->rx_len > (uint32_t)UINT16_MAX))
        return I2C_PORT_PARAM(RET_R_RANGE_ERR);

    if ((xfer->flags & HAL_I2C_XFER_NO_STOP) != 0u) return I2C_PORT_PARAM(RET_R_UNSUPPORTED);
    if ((xfer->tx_len > 0u) && (xfer->rx_len > 0u)) return I2C_PORT_PARAM(RET_R_UNSUPPORTED);

    if (!ctx->use_dma && !ctx->use_irq) return I2C_PORT_PARAM(RET_R_UNSUPPORTED);

    if (ctx->use_dma) {
        if ((xfer->tx_len > 0u) && (ctx->bsp.hdma_tx == NULL)) return I2C_PORT_STATE(RET_R_NOT_READY);
        if ((xfer->rx_len > 0u) && (ctx->bsp.hdma_rx == NULL)) return I2C_PORT_STATE(RET_R_NOT_READY);
    }
    return RET_OK;
}

/**
 * @brief 打开 I2C port 并完成 DMA/IRQ 资源绑定
 * @details
 * 1. 获取 BSP 资源映射；
 * 2. 按配置关联 DMA 句柄与 Parent；
 * 3. 按配置设置/使能 NVIC；
 * 4. 注册到全局 ctx 表，供 HAL 回调反查。
 */
ret_code_t hal_i2c_port_open(const hal_i2c_bus_cfg_t *cfg, hal_i2c_port_ctx_t *out) {
    /* 非空检查 */
    REQUIRE_RET((cfg != NULL) && (out != NULL), I2C_PORT_PARAM(RET_R_NULL_PTR));
    memset(out, 0, sizeof(*out));
    /* 必须选择一个异步能力 */
    if (!cfg->use_dma && !cfg->use_irq) return I2C_PORT_PARAM(RET_R_UNSUPPORTED);

    const ret_code_t rc = stm32_i2c_bsp_get(cfg->bus_id, &out->bsp);
    if (ret_is_err(rc)) return rc;
    if (!out->bsp.hi2c) return I2C_PORT_STATE(RET_R_NOT_READY);

    out->use_dma = cfg->use_dma;
    out->use_irq = cfg->use_irq;

    if (out->use_dma) {
        REQUIRE_RET((out->bsp.hdma_tx != NULL) || (out->bsp.hdma_rx != NULL),
                    I2C_PORT_STATE(RET_R_NOT_READY));

        if (out->bsp.hdma_tx && (out->bsp.hi2c->hdmatx == NULL)) {
            out->bsp.hi2c->hdmatx     = out->bsp.hdma_tx;
            out->bsp.hdma_tx->Parent  = out->bsp.hi2c;
        }
        if (out->bsp.hdma_rx && (out->bsp.hi2c->hdmarx == NULL)) {
            out->bsp.hi2c->hdmarx     = out->bsp.hdma_rx;
            out->bsp.hdma_rx->Parent  = out->bsp.hi2c;
        }
    }

    if (out->use_irq) {
        if ((int32_t)out->bsp.i2c_ev_irq < 0 || (int32_t)out->bsp.i2c_er_irq < 0)
            return I2C_PORT_STATE(RET_R_NOT_READY);
        HAL_NVIC_SetPriority(out->bsp.i2c_ev_irq, out->bsp.irq_prio, out->bsp.irq_sub_prio);
        HAL_NVIC_EnableIRQ(out->bsp.i2c_ev_irq);
        HAL_NVIC_SetPriority(out->bsp.i2c_er_irq, out->bsp.irq_prio, out->bsp.irq_sub_prio);
        HAL_NVIC_EnableIRQ(out->bsp.i2c_er_irq);
    }

    int32_t slot         = -1;
    osal_crit_state_t cs = 0u;
    OSAL_enter_critical_ex(&cs);
    slot = find_free_slot();
    if (slot >= 0) s_i2c_ctxs[slot] = out;
    OSAL_exit_critical_ex(cs);
    if (slot < 0) return I2C_PORT_RES(RET_R_NO_RESOURCE);

    out->opened = true;
    return RET_OK;
}

/**
 * @brief 关闭 I2C port，释放注册槽和 NVIC 配置
 * @note 仅在无活跃事务时允许关闭
 */
ret_code_t hal_i2c_port_close(hal_i2c_port_ctx_t *ctx) {
    REQUIRE_RET(ctx != NULL, I2C_PORT_PARAM(RET_R_NULL_PTR));
    if (!ctx->opened) return I2C_PORT_STATE(RET_R_NOT_READY);
    if (ctx->xfer_busy) return I2C_PORT_STATE(RET_R_BUSY);

    osal_crit_state_t cs = 0u;
    OSAL_enter_critical_ex(&cs);
    for (uint32_t i = 0; i < HAL_I2C_BUS_MAX; i++) {
        if (s_i2c_ctxs[i] == ctx) {
            s_i2c_ctxs[i] = NULL;
            break;
        }
    }
    OSAL_exit_critical_ex(cs);

    if (ctx->use_irq) {
        HAL_NVIC_DisableIRQ(ctx->bsp.i2c_ev_irq);
        HAL_NVIC_DisableIRQ(ctx->bsp.i2c_er_irq);
    }

    if (ctx->bsp.hi2c) {
        (void)HAL_I2C_DeInit(ctx->bsp.hi2c);
    }
    memset(ctx, 0, sizeof(*ctx));
    return RET_OK;
}

/**
 * @brief 注册 port 层事件回调
 * @note cb 可为 NULL，用于关闭上报
 */
ret_code_t hal_i2c_port_set_evt_cb(hal_i2c_port_ctx_t *ctx, hal_i2c_port_evt_cb_t cb, void *user) {
    REQUIRE_RET(ctx != NULL, I2C_PORT_PARAM(RET_R_NULL_PTR));
    ctx->evt_cb   = cb;
    ctx->evt_user = user;
    return RET_OK;
}

/**
 * @brief 将设备参数应用到 I2C 外设（按缓存避免重复 Init）
 * @details 当 addr_mode/hz/no_stretch/general_call 均未变化时，直接复用当前配置
 */
ret_code_t hal_i2c_port_apply(hal_i2c_port_ctx_t *ctx, const hal_i2c_dev_cfg_t *dev_cfg,
                              uint32_t bus_default_hz) {
    REQUIRE_RET((ctx != NULL) && (dev_cfg != NULL), I2C_PORT_PARAM(RET_R_NULL_PTR));
    if (!ctx->opened) return I2C_PORT_STATE(RET_R_NOT_READY);
    if (ctx->xfer_busy) return I2C_PORT_STATE(RET_R_BUSY);
    if (!ctx->bsp.hi2c) return I2C_PORT_STATE(RET_R_NOT_READY);

    const uint32_t hz = (dev_cfg->max_hz < bus_default_hz) ? dev_cfg->max_hz : bus_default_hz;
    if (hz == 0u) return I2C_PORT_PARAM(RET_R_RANGE_ERR);

    if (ctx->cfg_cache_valid && (ctx->cur_addr_mode == dev_cfg->addr_mode) && (ctx->cur_hz == hz) &&
        (ctx->cur_no_stretch == dev_cfg->no_stretch) &&
        (ctx->cur_general_call == dev_cfg->general_call)) {
        return RET_OK;
    }

    I2C_HandleTypeDef *h = ctx->bsp.hi2c;
    (void)HAL_I2C_DeInit(h);

    h->Init.ClockSpeed      = hz;
    h->Init.DutyCycle       = (hz > 100000u) ? I2C_DUTYCYCLE_16_9 : I2C_DUTYCYCLE_2;
    h->Init.OwnAddress1     = 0u;
    h->Init.AddressingMode  = (dev_cfg->addr_mode == HAL_I2C_ADDR_10BIT) ? I2C_ADDRESSINGMODE_10BIT
                                                                          : I2C_ADDRESSINGMODE_7BIT;
    h->Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    h->Init.OwnAddress2     = 0u;
    h->Init.GeneralCallMode = dev_cfg->general_call ? I2C_GENERALCALL_ENABLE : I2C_GENERALCALL_DISABLE;
    h->Init.NoStretchMode   = dev_cfg->no_stretch ? I2C_NOSTRETCH_ENABLE : I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(h) != HAL_OK) return I2C_PORT_IO(RET_R_HW_FAULT);

    ctx->cfg_cache_valid = true;
    ctx->cur_addr_mode   = dev_cfg->addr_mode;
    ctx->cur_hz          = hz;
    ctx->cur_no_stretch  = dev_cfg->no_stretch;
    ctx->cur_general_call = dev_cfg->general_call;
    return RET_OK;
}

/**
 * @brief 发起一次 I2C 异步事务（DMA/IT）
 * @details
 * 1. 事务参数检查；
 * 2. DMA 缓冲区 cache 维护（按需）；
 * 3. 缓存活跃事务上下文；
 * 4. 调用 HAL_I2C_*_DMA/IT 发起异步事务。
 */
ret_code_t hal_i2c_port_xfer(hal_i2c_port_ctx_t *ctx, const hal_i2c_dev_cfg_t *dev_cfg,
                             const hal_i2c_xfer_t *xfer) {
    REQUIRE_RET((ctx != NULL) && (dev_cfg != NULL) && (xfer != NULL), I2C_PORT_PARAM(RET_R_NULL_PTR));
    if (!ctx->opened) return I2C_PORT_STATE(RET_R_NOT_READY);
    if (ctx->xfer_busy) return I2C_PORT_STATE(RET_R_BUSY);
    if (!ctx->bsp.hi2c) return I2C_PORT_STATE(RET_R_NOT_READY);

    const ret_code_t vrc = validate_xfer_runtime(ctx, dev_cfg, xfer);
    if (ret_is_err(vrc)) return vrc;

    I2C_HandleTypeDef *h = ctx->bsp.hi2c;
    const uint16_t hal_dev_addr = i2c_to_hal_addr(dev_cfg);

    if (ctx->use_dma) {
        if (xfer->tx_len > 0u) stm32_i2c_dma_tx_clean(xfer->tx, xfer->tx_len);
        if (xfer->rx_len > 0u) stm32_i2c_dma_rx_invalidate(xfer->rx, xfer->rx_len);
    }

    ctx->xfer_busy       = 1u;
    ctx->active_dev_addr = hal_dev_addr;
    ctx->active_tx_len   = xfer->tx_len;
    ctx->active_rx_len   = xfer->rx_len;
    ctx->active_tx       = (const uint8_t *)xfer->tx;
    ctx->active_rx       = (uint8_t *)xfer->rx;

    HAL_StatusTypeDef st = HAL_ERROR;
    if (xfer->tx_len > 0u) {
        if (ctx->use_dma) {
            st = HAL_I2C_Master_Transmit_DMA(h, hal_dev_addr, (uint8_t *)xfer->tx,
                                             (uint16_t)xfer->tx_len);
        } else if (ctx->use_irq) {
            st = HAL_I2C_Master_Transmit_IT(h, hal_dev_addr, (uint8_t *)xfer->tx,
                                            (uint16_t)xfer->tx_len);
        }
    } else {
        if (ctx->use_dma) {
            st = HAL_I2C_Master_Receive_DMA(h, hal_dev_addr, (uint8_t *)xfer->rx,
                                            (uint16_t)xfer->rx_len);
        } else if (ctx->use_irq) {
            st = HAL_I2C_Master_Receive_IT(h, hal_dev_addr, (uint8_t *)xfer->rx,
                                           (uint16_t)xfer->rx_len);
        }
    }

    if (st != HAL_OK) {
        clear_xfer_ctx(ctx);
        return map_hal_status(st);
    }
    return RET_OK;
}

/**
 * @brief 强制中止当前 I2C 事务
 * @param ctx         port 句柄
 * @param disable_i2c true: 仅 DeInit；false: DeInit 后立即 Init 恢复
 * @return RET_OK 或错误码
 * @note 不管是否有活跃事务，disable_i2c=true 都会尝试关闭外设
 */
ret_code_t hal_i2c_port_abort(hal_i2c_port_ctx_t *ctx, bool disable_i2c) {
    REQUIRE_RET(ctx != NULL, I2C_PORT_PARAM(RET_R_NULL_PTR));
    if (!ctx->opened) return I2C_PORT_STATE(RET_R_NOT_READY);
    if (!ctx->bsp.hi2c) return I2C_PORT_STATE(RET_R_NOT_READY);

    if (!ctx->xfer_busy) {
        if (disable_i2c) {
            if (HAL_I2C_DeInit(ctx->bsp.hi2c) != HAL_OK) return I2C_PORT_IO(RET_R_HW_FAULT);
            ctx->cfg_cache_valid = false;
        }
        return RET_OK;
    }

    if (HAL_I2C_DeInit(ctx->bsp.hi2c) != HAL_OK) return I2C_PORT_IO(RET_R_HW_FAULT);

    if (!disable_i2c) {
        if (HAL_I2C_Init(ctx->bsp.hi2c) != HAL_OK) return I2C_PORT_IO(RET_R_HW_FAULT);
    } else {
        ctx->cfg_cache_valid = false;
    }
    clear_xfer_ctx(ctx);
    return RET_OK;
}

/**
 * @brief I2C Master TX 完成钩子
 * @param hi2c HAL I2C 句柄
 * @note 上报 DONE(tx=active_tx_len, rx=0)
 */
void hal_i2c_port_master_tx_cplt_hook(I2C_HandleTypeDef *hi2c) {
    hal_i2c_port_ctx_t *ctx = find_ctx_by_hi2c(hi2c);
    if (!ctx || !ctx->xfer_busy) return;
    emit_port_evt(ctx, HAL_I2C_PORT_EVT_DONE, RET_OK, ctx->active_tx_len, 0u, true);
}

/**
 * @brief I2C Master RX 完成钩子
 * @param hi2c HAL I2C 句柄
 * @note DMA 场景会先做 RX cache invalidate，再上报 DONE
 */
void hal_i2c_port_master_rx_cplt_hook(I2C_HandleTypeDef *hi2c) {
    hal_i2c_port_ctx_t *ctx = find_ctx_by_hi2c(hi2c);
    if (!ctx || !ctx->xfer_busy) return;
    if (ctx->use_dma && (ctx->active_rx != NULL) && (ctx->active_rx_len > 0u)) {
        stm32_i2c_dma_rx_invalidate(ctx->active_rx, ctx->active_rx_len);
    }
    emit_port_evt(ctx, HAL_I2C_PORT_EVT_DONE, RET_OK, 0u, ctx->active_rx_len, true);
}

/**
 * @brief I2C 错误回调钩子
 * @param hi2c HAL I2C 句柄
 * @note 上报 ERROR 并清理活跃上下文
 */
void hal_i2c_port_error_hook(I2C_HandleTypeDef *hi2c) {
    hal_i2c_port_ctx_t *ctx = find_ctx_by_hi2c(hi2c);
    if (!ctx || !ctx->xfer_busy) return;
    const ret_code_t rc = map_hal_i2c_error(hi2c->ErrorCode);
    emit_port_evt(ctx, HAL_I2C_PORT_EVT_ERROR, rc, 0u, 0u, true);
}

#if defined(CFG_PARAM_I2C_PORT_USE_LOCAL_HAL_CALLBACKS) && \
    (CFG_PARAM_I2C_PORT_USE_LOCAL_HAL_CALLBACKS == 1)
/**
 * @brief HAL Master TX 完成回调桥接
 */
void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c) {
    hal_i2c_port_master_tx_cplt_hook(hi2c);
}

/**
 * @brief HAL Master RX 完成回调桥接
 */
void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *hi2c) {
    hal_i2c_port_master_rx_cplt_hook(hi2c);
}

/**
 * @brief HAL Error 回调桥接
 */
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c) {
    hal_i2c_port_error_hook(hi2c);
}
#endif

#endif
