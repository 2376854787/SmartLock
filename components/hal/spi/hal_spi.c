/**
 * @file hal_spi.c
 * @brief hal抽象层逻辑代码
 * @details 细节
 * @author yan
 * @version v1.0
 * @date 2026年-2月-25日
 * @copyright 版权
 */
#include "APP_config.h"
#if defined(CFG_FEAT_HAL_SPI) && (CFG_FEAT_HAL_SPI == 1)

#include <limits.h>
#include <string.h>

#include "SchM_Spi.h"
#include "assert_cus.h"
#include "hal_gpio.h"
#include "hal_spi.h"
#include "hal_spi_internal.h"
#include "hal_spi_port.h"
#include "hal_time.h"
#include "osal.h"
#if defined(CFG_FEAT_LOG_SYSTEM) && (CFG_FEAT_LOG_SYSTEM == 1)
#include "log.h"
#endif
/* ---------- 参数/状态码 ---------- */
#define SPI_RC_PARAM(reason_)   RET_MAKE_PARAM(RET_MOD_HAL, RET_SUB_HAL_SPI, (reason_))
#define SPI_RC_STATE(reason_)   RET_MAKE_STATE(RET_MOD_HAL, RET_SUB_HAL_SPI, (reason_))
#define SPI_RC_TIMEOUT(reason_) RET_MAKE_TIMEOUT(RET_MOD_HAL, RET_SUB_HAL_SPI, (reason_))
#define SPI_RC_IO(reason_)      RET_MAKE_IO(RET_MOD_HAL, RET_SUB_HAL_SPI, (reason_))
#define SPI_RC_RES(reason_)     RET_MAKE_RESOURCE(RET_MOD_HAL, RET_SUB_HAL_SPI, (reason_))

#ifndef HAL_SPI_DEV_MAX
#define HAL_SPI_DEV_MAX 16u
#endif

#ifndef CFG_PARAM_SPI_CS_DEASSERT_WAIT_BSY
#define CFG_PARAM_SPI_CS_DEASSERT_WAIT_BSY 1
#endif

#ifndef CFG_PARAM_SPI_CS_DEASSERT_WAIT_BSY_SPIN_MAX
#define CFG_PARAM_SPI_CS_DEASSERT_WAIT_BSY_SPIN_MAX 100000u
#endif
/* 总线 */
struct hal_spi_bus {
    bool initialized;             /* 判断当前总线是否已经初始化 */
    hal_spi_bus_cfg_t cfg;        /* 总线配置 DMA irq 默认速率 */
    SchM_Spi_LockHandleType lock; /* SchM 适配互斥锁 */
    bool lock_valid;              /* 判断当前是否是启动了RTOS 环境 */
    hal_spi_port_ctx_t port;      /* 板级资源 */

    volatile uint8_t xfer_busy; /* 当前是否有异步事务在进行 */
    hal_spi_dev_t *active_dev;  /* 当前活跃设备 */
    uint32_t active_flags;      /* 当前事务 flags（KEEP_CS/NO_CS/STREAM/HW_STREAM） */
};
/* 设备 */
struct hal_spi_dev {
    bool in_use;           /* 设备是否被挂载 */
    hal_spi_bus_t *bus;    /* 挂载的总线 */
    hal_spi_dev_cfg_t cfg; /* 设备配置 */
    hal_gpio_t *cs;        /* 片选线句柄 */
    bool cs_valid;         /* 是否可以手动翻转片选线 */

    hal_spi_evt_cb_t evt_cb;             /* 异步事件回调 */
    void *evt_user;                      /* 回调用户上下文 */
    hal_spi_sync_observer_t sync_obs_cb; /* 同步观察者（供 sync 模块等待） */
    void *sync_obs_user;
};

/* 总线资源 */
static struct hal_spi_bus s_buses[HAL_SPI_BUS_MAX];
/* 设备资源 */
static struct hal_spi_dev s_devs[HAL_SPI_DEV_MAX];

/**
 * @brief 将 port 层错误码映射为 HAL SPI 统一错误码
 * @param rc_port port 层返回值
 * @return HAL 层语义错误码
 */
__attribute__((weak)) void hal_spi_on_port_error(ret_code_t rc_port, ret_code_t rc_hal,
                                                 const char *api, uint32_t arg0, uint32_t arg1) {
#if defined(CFG_FEAT_LOG_SYSTEM) && (CFG_FEAT_LOG_SYSTEM == 1) && \
    defined(CFG_PARAM_SPI_LOG_PORT_ERR) && (CFG_PARAM_SPI_LOG_PORT_ERR == 1)
#if defined(CFG_PARAM_SPI_LOG_PORT_ERR_IN_ISR) && (CFG_PARAM_SPI_LOG_PORT_ERR_IN_ISR == 1)
    if (ret_is_err(rc_port)) {
#else
    if (ret_is_err(rc_port) && !OSAL_in_isr()) {
#endif
        LOG_E("HAL_SPI", "api:%s port:0x%08lX->hal:0x%08lX cls:%u reason:%u arg0:%lu arg1:%lu",
              (api != NULL) ? api : "unknown", (unsigned long)rc_port, (unsigned long)rc_hal,
              (unsigned)RET_CLASS(rc_port), (unsigned)RET_REASON(rc_port), (unsigned long)arg0,
              (unsigned long)arg1);
    }
#else
    (void)rc_port;
    (void)rc_hal;
    (void)api;
    (void)arg0;
    (void)arg1;
#endif
}

static inline ret_code_t spi_map_port_to_hal(ret_code_t rc_port, const char *api, uint32_t arg0,
                                             uint32_t arg1) {
    if (ret_is_ok(rc_port)) return RET_OK;

    ret_code_t rc_hal = SPI_RC_IO(RET_R_IO);

    if (ret_is_class(rc_port, RET_CLASS_PARAM)) {
        if (ret_is_reason(rc_port, RET_R_NULL_PTR))
            rc_hal = SPI_RC_PARAM(RET_R_NULL_PTR);
        else if (ret_is_reason(rc_port, RET_R_RANGE_ERR))
            rc_hal = SPI_RC_PARAM(RET_R_RANGE_ERR);
        else if (ret_is_reason(rc_port, RET_R_UNSUPPORTED))
            rc_hal = SPI_RC_PARAM(RET_R_UNSUPPORTED);
        else
            rc_hal = SPI_RC_PARAM(RET_R_INVALID_ARG);
    } else if (ret_is_class(rc_port, RET_CLASS_TIMEOUT)) {
        rc_hal = SPI_RC_TIMEOUT(RET_R_TIMEOUT);
    } else if (ret_is_class(rc_port, RET_CLASS_RESOURCE)) {
        if (ret_is_reason(rc_port, RET_R_NO_MEM))
            rc_hal = SPI_RC_RES(RET_R_NO_MEM);
        else
            rc_hal = SPI_RC_RES(RET_R_NO_RESOURCE);
    } else if (ret_is_class(rc_port, RET_CLASS_STATE)) {
        if (ret_is_reason(rc_port, RET_R_BUSY))
            rc_hal = SPI_RC_STATE(RET_R_BUSY);
        else if (ret_is_reason(rc_port, RET_R_NOT_READY))
            rc_hal = SPI_RC_STATE(RET_R_NOT_READY);
        else
            rc_hal = SPI_RC_STATE(RET_R_STATE_ERR);
    }

    hal_spi_on_port_error(rc_port, rc_hal, api, arg0, arg1);
    return rc_hal;
}

static ret_code_t spi_map_runtime_to_hal(ret_code_t rc_runtime) {
    if (ret_is_ok(rc_runtime)) return RET_OK;

    if (ret_is_class(rc_runtime, RET_CLASS_PARAM)) {
        if (ret_is_reason(rc_runtime, RET_R_NULL_PTR))
            return SPI_RC_PARAM(RET_R_NULL_PTR);
        else if (ret_is_reason(rc_runtime, RET_R_RANGE_ERR))
            return SPI_RC_PARAM(RET_R_RANGE_ERR);
        else
            return SPI_RC_PARAM(RET_R_INVALID_ARG);
    }

    if (ret_is_class(rc_runtime, RET_CLASS_TIMEOUT)) return SPI_RC_TIMEOUT(RET_R_TIMEOUT);

    if (ret_is_class(rc_runtime, RET_CLASS_RESOURCE)) {
        if (ret_is_reason(rc_runtime, RET_R_NO_MEM))
            return SPI_RC_RES(RET_R_NO_MEM);
        else
            return SPI_RC_RES(RET_R_NO_RESOURCE);
    }

    if (ret_is_class(rc_runtime, RET_CLASS_STATE)) {
        if (ret_is_reason(rc_runtime, RET_R_BUSY))
            return SPI_RC_STATE(RET_R_BUSY);
        else if (ret_is_reason(rc_runtime, RET_R_NOT_READY))
            return SPI_RC_STATE(RET_R_NOT_READY);
        else
            return SPI_RC_STATE(RET_R_STATE_ERR);
    }

    return SPI_RC_IO(RET_R_IO);
}

/**
 * @brief 判断当前总线是否仍有设备挂载
 * @param bus 总线句柄
 * @return true: 仍有设备，false: 无设备
 */
static bool bus_has_attached_dev(const hal_spi_bus_t *bus) {
    if (!bus) return false;
    SchM_Spi_CritStateType cs = 0u;
    bool found                = false;
    SchM_Enter_Spi_ExclusiveArea(&cs);
    for (uint32_t i = 0; i < HAL_SPI_DEV_MAX; i++) {
        if (s_devs[i].in_use && (s_devs[i].bus == bus)) {
            found = true;
            break;
        }
    }
    SchM_Exit_Spi_ExclusiveArea(cs);
    return found;
}
/**
 * @brief 找到一个没有被使用的 设备对象池 进行填充配置 并声明该对象已被使用
 * @param bus 总线句柄
 * @param cfg 设备配置
 * @return
 */
static hal_spi_dev_t *reserve_dev_slot(hal_spi_bus_t *bus, const hal_spi_dev_cfg_t *cfg) {
    if (!bus || !cfg) return NULL;

    hal_spi_dev_t *d          = NULL;
    SchM_Spi_CritStateType cs = 0u;
    SchM_Enter_Spi_ExclusiveArea(&cs);
    /* 找到一个没有被使用的 设备对象池 进行填充配置 */
    for (uint32_t i = 0; i < HAL_SPI_DEV_MAX; i++) {
        if (!s_devs[i].in_use) {
            d = &s_devs[i];
            memset(d, 0, sizeof(*d));
            d->in_use = true;
            d->bus    = bus;
            d->cfg    = *cfg;
            break;
        }
    }
    SchM_Exit_Spi_ExclusiveArea(cs);
    return d;
}

static void release_dev_slot(hal_spi_dev_t *d) {
    if (!d) return;
    SchM_Spi_CritStateType cs = 0u;
    SchM_Enter_Spi_ExclusiveArea(&cs);
    memset(d, 0, sizeof(*d));
    SchM_Exit_Spi_ExclusiveArea(cs);
}
/**
 * @brief 总线加锁
 * @param b 总线句柄
 * @param timeout_ms 锁获取的超时时间
 * @return 32位状态码
 */
static ret_code_t bus_lock(hal_spi_bus_t *b, uint32_t timeout_ms) {
    REQUIRE_RET(b != NULL, SPI_RC_PARAM(RET_R_NULL_PTR));
    if (!b->lock_valid) return RET_OK;
    return SchM_Spi_Lock(b->lock, timeout_ms);
}
/**
 * @brief 总线解锁
 * @param b 总线句柄
 */
static void bus_unlock(hal_spi_bus_t *b) {
    CORE_ASSERT(b != NULL);
    if (!b) return;
    if (!b->lock_valid) return;
    SchM_Spi_Unlock(b->lock);
}
/**
 * @brief SPI 设备事件分发
 * @param d 设备句柄
 * @param evt 事件载体
 */
static inline void emit_dev_evt(const hal_spi_dev_t *d, const hal_spi_event_t *evt) {
    if (!d || !evt) return;
    /* 先通知同步观察者，保证同步等待能稳定捕获 DONE/ERROR 终态，
     * 再走设备级回调。 */
    if (d->sync_obs_cb) d->sync_obs_cb(d->sync_obs_user, evt);

    if (d->evt_cb) {
#if defined(CFG_PARAM_SPI_CB_IN_ISR) && (CFG_PARAM_SPI_CB_IN_ISR == 1)
        d->evt_cb(d->evt_user, evt);
#else
        if (!OSAL_in_isr()) d->evt_cb(d->evt_user, evt);
#endif
    }
}
/**
 * @brief 片选线选中
 * @param d 设备句柄
 */
static void cs_assert(hal_spi_dev_t *d) {
    /* 确保片选线有效 */
    if (!d || !d->cs_valid) return;
    /* 选择片选线 */
    if (d->cfg.cs_active_low)
        hal_gpio_write(d->cs, HAL_GPIO_LEVEL_LOW);
    else
        hal_gpio_write(d->cs, HAL_GPIO_LEVEL_HIGH);
    /* 延时指定的建立时间 */
    if (d->cfg.cs_setup_us) hal_time_delay_us(d->cfg.cs_setup_us);
}
/**
 * @brief 片选线取消选中
 * @param d 设备句柄
 */
static void cs_deassert(hal_spi_dev_t *d) {
    if (!d || !d->cs_valid) return;
    /* ISR 场景不做 hold delay，避免拉长中断执行时间 */
    if (d->cfg.cs_hold_us && !OSAL_in_isr()) hal_time_delay_us(d->cfg.cs_hold_us);
    /* 取消选中片选 */
    if (d->cfg.cs_active_low)
        hal_gpio_write(d->cs, HAL_GPIO_LEVEL_HIGH);
    else
        hal_gpio_write(d->cs, HAL_GPIO_LEVEL_LOW);
}

/**
 * @brief 片选释放前等待底层 SPI 空闲（由 port 层实现平台细节）
 * @param d 设备句柄
 * @note 若等待失败仅记录/上报错误，不阻断 CS 释放流程
 */
static void wait_spi_idle_before_cs_deassert(const hal_spi_dev_t *d) {
#if defined(CFG_PARAM_SPI_CS_DEASSERT_WAIT_BSY) && (CFG_PARAM_SPI_CS_DEASSERT_WAIT_BSY == 1)
    if (!d || !d->cs_valid || !d->bus) return;
    /* ISR 路径禁止自旋等待，避免放大中断时延 */
    if (OSAL_in_isr()) return;
    const uint32_t spin = (uint32_t)CFG_PARAM_SPI_CS_DEASSERT_WAIT_BSY_SPIN_MAX;
    const ret_code_t rc = hal_spi_port_wait_idle(&d->bus->port, spin);
    if (ret_is_err(rc)) {
        (void)spi_map_port_to_hal(rc, "hal_spi_port_wait_idle", d->bus->cfg.bus_id, spin);
    }
#else
    (void)d;
#endif
}
/**
 * @brief 检查总线的配置 DMA irq 默认速率
 * @param cfg 总线配置
 * @return
 */
static ret_code_t cfg_check_bus(const hal_spi_bus_cfg_t *cfg) {
    /* 排除 NULL */
    REQUIRE_RET(cfg != NULL, SPI_RC_PARAM(RET_R_NULL_PTR));
    /* 检查 bus_id 是否在有效范围内 */
    REQUIRE_RET(cfg->bus_id < HAL_SPI_BUS_MAX, SPI_RC_PARAM(RET_R_RANGE_ERR));
    /* 确保合法速率 */
    REQUIRE_RET(cfg->default_hz != 0u, SPI_RC_PARAM(RET_R_RANGE_ERR));
    return RET_OK;
}
/**
 * @brief 检查 SPI 设备的配置
 * @param cfg 设备句柄
 * @return
 */
static ret_code_t cfg_check_dev(const hal_spi_dev_cfg_t *cfg) {
    /* 非 NULL */
    REQUIRE_RET(cfg != NULL, SPI_RC_PARAM(RET_R_NULL_PTR));
    /* 模式合法检查 */
    REQUIRE_RET(cfg->mode <= HAL_SPI_MODE3, SPI_RC_PARAM(RET_R_INVALID_ARG));
    /* 大小端合法检查 */
    REQUIRE_RET(
        (cfg->bit_order == HAL_SPI_BITORDER_MSB) || (cfg->bit_order == HAL_SPI_BITORDER_LSB),
        SPI_RC_PARAM(RET_R_INVALID_ARG));
    /* 最大速率 大于0 */
    REQUIRE_RET(cfg->max_hz != 0u, SPI_RC_PARAM(RET_R_RANGE_ERR));
    /* 位宽合法检查 */
    REQUIRE_RET((cfg->frame_bits == HAL_SPI_FRAME_8) || (cfg->frame_bits == HAL_SPI_FRAME_16),
                SPI_RC_PARAM(RET_R_INVALID_ARG));
    /* 双工方向合法检查 */
    REQUIRE_RET((cfg->dir == HAL_SPI_DIR_2LINES) || (cfg->dir == HAL_SPI_DIR_2LINES_RXONLY) ||
                    (cfg->dir == HAL_SPI_DIR_LINE),
                SPI_RC_PARAM(RET_R_INVALID_ARG));
    /* 片选类型合法检查 */
    REQUIRE_RET((cfg->cs_type == HAL_SPI_CS_GPIO) || (cfg->cs_type == HAL_SPI_CS_HW),
                SPI_RC_PARAM(RET_R_INVALID_ARG));
    if (cfg->cs_type == HAL_SPI_CS_GPIO) {
        REQUIRE_RET(cfg->cs_gpio_id != 0u, SPI_RC_PARAM(RET_R_RANGE_ERR));
    }
    return RET_OK;
}
/**
 * @brief 检查句柄参数、事务参数
 * @param dev 设备句柄
 * @param xfer 事务
 * @param hw_stream_api 是否是数据流的api
 * @return 状态码
 */
static ret_code_t validate_api_xfer_common(const hal_spi_dev_t *dev, const hal_spi_xfer_t *xfer,
                                           bool hw_stream_api) {
    /* 句柄非空检查 */
    REQUIRE_RET((dev != NULL) && (xfer != NULL), SPI_RC_PARAM(RET_R_NULL_PTR));
    /* 必须设备被使用和以及绑定的总线被初始化 */
    if (!dev->in_use || (dev->bus == NULL) || !dev->bus->initialized)
        return SPI_RC_STATE(RET_R_NOT_READY);
    /* 长度必须有效 */
    REQUIRE_RET(xfer->len != 0u, SPI_RC_PARAM(RET_R_RANGE_ERR));
    /* 发送和接收地址不能同时为空 */
    REQUIRE_RET((xfer->tx != NULL) || (xfer->rx != NULL), SPI_RC_PARAM(RET_R_INVALID_ARG));
    /* 硬件流api调用必查看是不是 end事务*/
    if (hw_stream_api) {
        REQUIRE_RET((xfer->flags & HAL_SPI_XFER_STREAM_END) == 0u, SPI_RC_PARAM(RET_R_INVALID_ARG));
    } else {
        /* 非硬件api 就不能有硬件流事务*/
        REQUIRE_RET((xfer->flags & HAL_SPI_XFER_HW_STREAM) == 0u, SPI_RC_PARAM(RET_R_UNSUPPORTED));
        /* 非硬件流 且是流结束事务 && 不是软件流 返回错误 */
        if ((xfer->flags & HAL_SPI_XFER_STREAM_END) && !(xfer->flags & HAL_SPI_XFER_STREAM))
            return SPI_RC_PARAM(RET_R_INVALID_ARG);
    }
    /* 检查位宽和发送的长度是否对的上 */
    const uint32_t frame_bytes = (dev->cfg.frame_bits == HAL_SPI_FRAME_16) ? 2u : 1u;
    REQUIRE_RET((xfer->len % frame_bytes) == 0u, SPI_RC_PARAM(RET_R_RANGE_ERR));
    REQUIRE_RET((xfer->len / frame_bytes) <= (uint32_t)UINT16_MAX, SPI_RC_PARAM(RET_R_RANGE_ERR));

    return RET_OK;
}

/**
 * @brief 原子申请总线当前事务槽位
 * @param b 总线句柄
 * @param d 当前活跃设备
 * @param flags 当前事务 flags
 * @return 32位状态码
 */
static ret_code_t bus_claim_active_xfer(hal_spi_bus_t *b, hal_spi_dev_t *d, uint32_t flags) {
    SchM_Spi_CritStateType cs = 0u;
    SchM_Enter_Spi_ExclusiveArea(&cs);
    if (b->xfer_busy) {
        SchM_Exit_Spi_ExclusiveArea(cs);
        return SPI_RC_STATE(RET_R_BUSY);
    }
    b->xfer_busy    = 1u;
    b->active_dev   = d;
    b->active_flags = flags;
    SchM_Exit_Spi_ExclusiveArea(cs);
    return RET_OK;
}

/**
 * @brief 原子释放总线当前事务槽位
 * @param b 总线句柄
 */
static void bus_release_active_xfer(hal_spi_bus_t *b) {
    if (!b) return;
    SchM_Spi_CritStateType cs = 0u;
    SchM_Enter_Spi_ExclusiveArea(&cs);
    b->xfer_busy    = 0u;
    b->active_dev   = NULL;
    b->active_flags = 0u;
    SchM_Exit_Spi_ExclusiveArea(cs);
}

/**
 * @brief port 层事件回调
 * @param user 总线句柄
 * @param evt 事件载体
 */
static void spi_port_evt_cb(void *user, const hal_spi_port_evt_t *evt) {
    hal_spi_bus_t *b = (hal_spi_bus_t *)user;
    if (!b || !b->initialized || !evt) return;

    hal_spi_dev_t *dev        = NULL;
    uint32_t flags            = 0u;

    /* 原子读取当前活跃事务（先不清 busy，防止并发 detach 抢占） */
    SchM_Spi_CritStateType cs = 0u;
    SchM_Enter_Spi_ExclusiveArea(&cs);
    dev   = b->active_dev;
    flags = b->active_flags;
    SchM_Exit_Spi_ExclusiveArea(cs);

    if (!dev) {
        bus_release_active_xfer(b);
        return;
    }

    const bool no_cs      = (flags & HAL_SPI_XFER_NO_CS) != 0u;
    const bool keep       = (flags & HAL_SPI_XFER_KEEP_CS) != 0u;
    const bool stream     = (flags & HAL_SPI_XFER_STREAM) != 0u;
    const bool stream_end = (flags & HAL_SPI_XFER_STREAM_END) != 0u;
    const bool hw_stream  = (flags & HAL_SPI_XFER_HW_STREAM) != 0u;
    const bool hold_cs    = keep || (stream && !stream_end) || hw_stream;
    /* 1、硬件事务流 */
    if (evt->type == HAL_SPI_PORT_EVT_STREAM_HALF || evt->type == HAL_SPI_PORT_EVT_STREAM_FULL) {
        hal_spi_event_t sevt = {0};
        sevt.type            = (evt->type == HAL_SPI_PORT_EVT_STREAM_HALF) ? HAL_SPI_EVT_STREAM_HALF
                                                                           : HAL_SPI_EVT_STREAM_FULL;
        sevt.stream.bytes    = evt->bytes;
        emit_dev_evt(dev, &sevt);
        return;
    }
    /* 2、软件事务 */
    const ret_code_t rc_port = evt->rc_port;
    const uint32_t bytes     = evt->bytes;

    /* 失败时强制释放 CS；成功时按 keep/stream/no_cs 策略处理 */
    if (!no_cs) {
        if (ret_is_err(rc_port)) {
            cs_deassert(dev);
        } else if (!hold_cs) {
            wait_spi_idle_before_cs_deassert(dev);
            cs_deassert(dev);
        }
    }

    /* CS 处理完成后再释放事务槽位，避免并发 detach 干扰 CS 生命周期 */
    bus_release_active_xfer(b);

    /* 构建事件 */
    hal_spi_event_t hevt = {0};
    ret_code_t rc_hal    = RET_OK;
    if (ret_is_ok(rc_port)) {
        hevt.type       = HAL_SPI_EVT_DONE;
        hevt.done.bytes = bytes;
        rc_hal          = RET_OK;
    } else {
        hevt.type   = HAL_SPI_EVT_ERROR;
        rc_hal      = spi_map_port_to_hal(rc_port, "spi_port_evt_cb", (uint32_t)evt->type, bytes);
        hevt.err.rc = rc_hal;
    }

    /* 上报事件 给用户回调函数 */
    emit_dev_evt(dev, &hevt);
}

/**
 * @brief 获取 SPI 抽象句柄
 * @param cfg 总线配置
 * @param out_bus 返回板级的映射配置
 * @return 32位状态码
 */
ret_code_t hal_spi_bus_init(const hal_spi_bus_cfg_t *cfg, hal_spi_bus_t **out_bus) {
    /* 参数检查 */
    REQUIRE_RET(out_bus != NULL, SPI_RC_PARAM(RET_R_NULL_PTR));
    *out_bus      = NULL;

    /* 检查总线的配置 */
    ret_code_t rc = cfg_check_bus(cfg);
    if (ret_is_err(rc)) return rc;
    /* id必须没有使用 */
    if (s_buses[cfg->bus_id].initialized) {
        return SPI_RC_RES(RET_R_NO_RESOURCE);
    }

    /* 从资源池获取到存储总线的地址 */
    hal_spi_bus_t *b = &s_buses[cfg->bus_id];
    /* 初始化 */
    memset(b, 0, sizeof(*b));
    b->initialized = true;
    b->cfg         = *cfg;

    /* 从 bsp 处填充真实的数据，并按 bus 配置初始化底层能力 */
    rc             = hal_spi_port_init(cfg, &b->port);
    if (ret_is_err(rc)) {
        b->initialized = false;
        return spi_map_port_to_hal(rc, "hal_spi_port_init", cfg->bus_id, cfg->default_hz);
    }
    /* 注册 port 异步完成回调 */
    rc = hal_spi_port_set_evt_cb(&b->port, spi_port_evt_cb, b);
    if (ret_is_err(rc)) {
        (void)hal_spi_port_deinit(&b->port);
        b->initialized = false;
        return spi_map_port_to_hal(rc, "hal_spi_port_set_evt_cb", cfg->bus_id, 0u);
    }

    /* RTOS 环境下创建互斥锁 */
    if (SchM_Spi_KernelIsRunning()) {
        if (ret_is_ok(SchM_Spi_LockCreate(&b->lock, "spi_bus", false, true))) {
            b->lock_valid = true;
        }
    }
    *out_bus = b;
    return RET_OK;
}
/**
 * @brief 将 Spi句柄、资源池、重置、底层硬件配置重置 并标记id为空闲
 * @param bus SPI抽象句柄
 * @return
 */
ret_code_t hal_spi_bus_deinit(hal_spi_bus_t *bus) {
    /* 参数检查 */
    REQUIRE_RET(bus != NULL, SPI_RC_PARAM(RET_R_NULL_PTR));
    if (!bus->initialized) return SPI_RC_STATE(RET_R_NOT_READY);
    /* 异步传输进行中 总线被占用不允许关闭总线 */
    if (bus->xfer_busy) return SPI_RC_STATE(RET_R_BUSY);
    /* 如果仍有设备挂在当前总线上，拒绝关闭总线 */
    if (bus_has_attached_dev(bus)) return SPI_RC_STATE(RET_R_BUSY);

    /* 释放资源 SPI句柄、DMA、*/
    const ret_code_t rc = hal_spi_port_deinit(&bus->port);
    if (ret_is_err(rc)) return spi_map_port_to_hal(rc, "hal_spi_port_deinit", bus->cfg.bus_id, 0u);

    /* 有互斥锁就删除掉 */
    if (bus->lock_valid) {
        SchM_Spi_LockDelete(bus->lock);
        bus->lock       = 0u;
        bus->lock_valid = false;
    }
    bus->initialized = false;
    return RET_OK;
}
/**
 * @brief 初始化设备句柄并将设备和总线进行绑定注册 填充资源池中的对象并返回这个对象 并且根据事务
 * @param bus 总线句柄
 * @param cfg 设备配置 通信模式、大小端、位宽、频率、片选id、片选类型、活动电平等
 * @param out_dev 返回设备句柄
 * @return 32位状态码
 */
ret_code_t hal_spi_dev_attach(hal_spi_bus_t *bus, const hal_spi_dev_cfg_t *cfg,
                              hal_spi_dev_t **out_dev) {
    /* 参数检查 */
    REQUIRE_RET(out_dev != NULL, SPI_RC_PARAM(RET_R_NULL_PTR));
    *out_dev = NULL;
    ASSERT_PARAM(bus != NULL);
    REQUIRE_RET(bus != NULL, SPI_RC_PARAM(RET_R_NULL_PTR));
    /* 确保总线已经初始化 */
    if (!bus->initialized) return SPI_RC_STATE(RET_R_NOT_READY);
    /* 检查设备的配置 */
    ret_code_t rc = cfg_check_dev(cfg);
    if (ret_is_err(rc)) return rc;
    /* 设备资源池中 填充配置并返回这个对象 */
    hal_spi_dev_t *d = reserve_dev_slot(bus, cfg);
    if (!d) return SPI_RC_RES(RET_R_NO_RESOURCE);

    /* 软件片选线 类型配置 */
    if (cfg->cs_type == HAL_SPI_CS_GPIO) {
        rc = hal_gpio_acquire(&d->cs, cfg->cs_gpio_id);
        if (ret_is_err(rc)) {
            release_dev_slot(d);
            return rc;
        }
        const hal_gpio_cfg_t gc = {
            .dir           = HAL_GPIO_DIR_OUT,
            .out_type      = HAL_GPIO_OUT_PP, /* CS 推挽 */
            .pull          = HAL_GPIO_PULL_UP,
            .speed         = HAL_GPIO_SPEED_VERY_HIGH,
            .irq           = HAL_GPIO_IRQ_NONE,
            .alternate     = HAL_GPIO_AF_NONE,
            .default_level = cfg->cs_active_low ? HAL_GPIO_LEVEL_HIGH : HAL_GPIO_LEVEL_LOW,
        };
        /* 配置参数 */
        rc = hal_gpio_config(d->cs, &gc);
        if (ret_is_err(rc)) {
            (void)hal_gpio_release(d->cs);
            release_dev_slot(d);
            return rc;
        }
        d->cs_valid = true;
    }

    *out_dev = d;
    return RET_OK;
}
/**
 * @brief SPI 设备注册回调
 * @param dev 设备句柄
 * @param cb 回调函数
 * @param user 用户上下文
 * @return 32位状态码
 */
ret_code_t hal_spi_dev_set_evt_cb(hal_spi_dev_t *dev, hal_spi_evt_cb_t cb, void *user) {
    REQUIRE_RET(dev != NULL, SPI_RC_PARAM(RET_R_NULL_PTR));
    if (!dev->in_use) return SPI_RC_STATE(RET_R_NOT_READY);
    dev->evt_cb   = cb;
    dev->evt_user = user;
    return RET_OK;
}

/**
 * @brief 注册/注销同步观察者（供同步封装模块使用）
 * @param dev 设备句柄
 * @param cb  观察者回调，传 NULL 表示注销
 * @param user 观察者上下文
 * @return 32位状态码
 */
ret_code_t hal_spi_dev_set_sync_observer(hal_spi_dev_t *dev, hal_spi_sync_observer_t cb,
                                         void *user) {
    REQUIRE_RET(dev != NULL, SPI_RC_PARAM(RET_R_NULL_PTR));
    if (!dev->in_use) return SPI_RC_STATE(RET_R_NOT_READY);

    /* 在临界区内切换观察者指针，避免与 ISR 事件分发并发竞争。 */
    SchM_Spi_CritStateType cs = 0u;
    SchM_Enter_Spi_ExclusiveArea(&cs);
    dev->sync_obs_cb   = cb;
    dev->sync_obs_user = user;
    SchM_Exit_Spi_ExclusiveArea(cs);
    return RET_OK;
}
/**
 * @brief 将设备和总线进行解绑
 * @return 32位状态码
 */
ret_code_t hal_spi_dev_detach(hal_spi_dev_t *dev) {
    REQUIRE_RET(dev != NULL, SPI_RC_PARAM(RET_R_NULL_PTR));
    if (!dev->in_use) return SPI_RC_STATE(RET_R_NOT_READY);

    /* 异步传输进行中不允许解绑当前设备 */
    if (dev->bus) {
        bool busy_active          = false;
        SchM_Spi_CritStateType cs = 0u;
        SchM_Enter_Spi_ExclusiveArea(&cs);
        /* 没有异步事物正在进行 且 当前解绑的设备不能是活跃设备 需要等回调调用
         * bus_release_active_xfer 释放事务*/
        busy_active = (dev->bus->xfer_busy != 0u) && (dev->bus->active_dev == dev);
        SchM_Exit_Spi_ExclusiveArea(cs);
        if (busy_active) return SPI_RC_STATE(RET_R_BUSY);
    }
    if (dev->cs_valid) {
        (void)hal_gpio_release(dev->cs);
    }
    release_dev_slot(dev);
    return RET_OK;
}

/**
 * @brief 在持锁状态下发起异步传输
 * @param d 设备句柄
 * @param x 传输配置
 * @return 32位状态码
 */
static ret_code_t spi_xfer_guarded(hal_spi_dev_t *d, const hal_spi_xfer_t *x) {
    hal_spi_bus_t *b = d->bus;
    /* 先占住事务槽位，避免无锁场景并发发起冲突 */
    ret_code_t rc    = bus_claim_active_xfer(b, d, x->flags);
    if (ret_is_err(rc)) return rc;

    /* 配置应用到硬件（port 负责实际寄存器/HAL init） */
    rc = hal_spi_port_apply(&b->port, &d->cfg, b->cfg.default_hz);
    if (ret_is_err(rc)) {
        bus_release_active_xfer(b);
        return spi_map_port_to_hal(rc, "hal_spi_port_apply", b->cfg.bus_id, x->len);
    }

    /* CS 选中 */
    const bool no_cs = (x->flags & HAL_SPI_XFER_NO_CS) != 0u;
    if (!no_cs) cs_assert(d);
    /* 发起一次异步传输 */
    rc = hal_spi_port_xfer(&b->port, x);
    if (ret_is_err(rc)) {
        /* 发起失败且 CS 已经拉低，需要立即释放 */
        if (!no_cs) cs_deassert(d);
        /* 发起失败时回滚活跃状态 */
        bus_release_active_xfer(b);
        return spi_map_port_to_hal(rc, "hal_spi_port_xfer", b->cfg.bus_id, x->len);
    }
    return RET_OK;
}
/**
 * @brief 硬件事务流开始发送
 * @param d 设备句柄
 * @param x 事务句柄
 * @return 32位状态码
 */
static ret_code_t spi_stream_start_guarded(hal_spi_dev_t *d, const hal_spi_xfer_t *x) {
    /* 获取总线句柄 */
    hal_spi_bus_t *b = d->bus;
    /* 检查 事物必须有硬件流 */
    uint32_t flags   = x->flags | HAL_SPI_XFER_HW_STREAM;
    /* 必须加上调整 cs线 */
    if ((flags & HAL_SPI_XFER_NO_CS) == 0u) flags |= HAL_SPI_XFER_KEEP_CS;
    /* 原子申请事务 占用总线*/
    ret_code_t rc = bus_claim_active_xfer(b, d, flags);
    if (ret_is_err(rc)) return rc;

    /* 底层应用配置 */
    rc = hal_spi_port_apply(&b->port, &d->cfg, b->cfg.default_hz);
    if (ret_is_err(rc)) {
        /* 释放事务 */
        bus_release_active_xfer(b);
        return spi_map_port_to_hal(rc, "hal_spi_port_apply", b->cfg.bus_id, x->len);
    }

    /* 没有no_cs 事务就选中cs */
    const bool no_cs = (flags & HAL_SPI_XFER_NO_CS) != 0u;
    if (!no_cs) cs_assert(d);

    /* 调用底层的数据流开始 */
    rc = hal_spi_port_stream_start(&b->port, x);
    if (ret_is_err(rc)) {
        if (!no_cs) cs_deassert(d);
        bus_release_active_xfer(b);
        return spi_map_port_to_hal(rc, "hal_spi_port_stream_start", b->cfg.bus_id, x->len);
    }
    return RET_OK;
}
/**
 * @brief 停止硬件DMA 根据配置决定是否关闭
 * @param d 设备句柄
 * @param disable_spi 是否是DeInit SPI
 * @return
 */
static ret_code_t spi_stream_stop_guarded(hal_spi_dev_t *d, bool disable_spi) {
    /* 获取总线 */
    hal_spi_bus_t *b = d->bus;
    /* 参数检查 */
    if (!b || !b->xfer_busy) return SPI_RC_STATE(RET_R_NOT_READY);
    if (b->active_dev != d) return SPI_RC_STATE(RET_R_BUSY);
    if ((b->active_flags & HAL_SPI_XFER_HW_STREAM) == 0u) return SPI_RC_STATE(RET_R_NOT_READY);
    /* 调用port 函数 */
    const ret_code_t rc = hal_spi_port_stream_stop(&b->port, disable_spi);
    if (ret_is_err(rc))
        return spi_map_port_to_hal(rc, "hal_spi_port_stream_stop", b->cfg.bus_id,
                                   disable_spi ? 1u : 0u);
    /* 根据事务 决定是否取消 选中 cs线 */
    const bool no_cs = (b->active_flags & HAL_SPI_XFER_NO_CS) != 0u;
    if (!no_cs) cs_deassert(d);
    bus_release_active_xfer(b);
    return RET_OK;
}

/**
 * @brief 在持锁状态下中止当前设备事务
 * @param d 设备句柄
 * @param disable_spi true: 中止后反初始化 SPI；false: 仅中止事务
 * @return RET_OK 或错误码
 */
static ret_code_t spi_abort_guarded(hal_spi_dev_t *d, bool disable_spi) {
    hal_spi_bus_t *b = d->bus;
    if (!b || !b->xfer_busy) return SPI_RC_STATE(RET_R_NOT_READY);
    if (b->active_dev != d) return SPI_RC_STATE(RET_R_BUSY);
    /* 底层 port */
    const uint32_t flags = b->active_flags;
    const ret_code_t rc  = hal_spi_port_abort(&b->port, disable_spi);
    if (ret_is_err(rc)) {
        return spi_map_port_to_hal(rc, "hal_spi_port_abort", b->cfg.bus_id, disable_spi ? 1u : 0u);
    }
    /* 据事务 是否取消选中 */
    const bool no_cs = (flags & HAL_SPI_XFER_NO_CS) != 0u;
    if (!no_cs) cs_deassert(d);
    /* 释放事务 */
    bus_release_active_xfer(b);

    /* 中止属于业务中断当前事务，统一上报 ERROR。 */
    const ret_code_t abort_rc = SPI_RC_STATE(RET_R_ABORTED);

    hal_spi_event_t evt       = {0};
    evt.type                  = HAL_SPI_EVT_ERROR;
    evt.err.rc                = abort_rc;
    emit_dev_evt(d, &evt);

    return RET_OK;
}
/**
 * @brief 进行一次异步的事物 立即返回
 * @param dev 设备句柄
 * @param xfer 事物配置
 * @return 32位状态码
 */
ret_code_t hal_spi_transceive(hal_spi_dev_t *dev, const hal_spi_xfer_t *xfer) {
    /* 参数检查 */
    ret_code_t rc = validate_api_xfer_common(dev, xfer, false);
    if (ret_is_err(rc)) return rc;
    hal_spi_bus_t *b = dev->bus;
    /* 加锁（仅保护发起路径，不在锁内等待硬件完成） */
    rc               = bus_lock(b, xfer->timeout_ms ? xfer->timeout_ms : OSAL_WAIT_FOREVER);
    if (ret_is_err(rc)) return spi_map_runtime_to_hal(rc);
    /* 更新配置并发起异步传输（发起成功即返回） */
    rc = spi_xfer_guarded(dev, xfer);
    bus_unlock(b);
    return rc;
}

/**
 * @brief 强制中止当前设备事务（普通异步/硬件流）
 * @param dev 设备句柄
 * @param disable_spi true: 中止后反初始化 SPI；false: 仅中止事务
 * @return RET_OK 或错误码
 */
ret_code_t hal_spi_abort(hal_spi_dev_t *dev, bool disable_spi) {
    REQUIRE_RET(dev != NULL, SPI_RC_PARAM(RET_R_NULL_PTR));
    if (!dev->in_use || (dev->bus == NULL) || !dev->bus->initialized)
        return SPI_RC_STATE(RET_R_NOT_READY);

    hal_spi_bus_t *b = dev->bus;
    ret_code_t rc    = bus_lock(b, OSAL_WAIT_FOREVER);
    if (ret_is_err(rc)) return spi_map_runtime_to_hal(rc);

    rc = spi_abort_guarded(dev, disable_spi);
    bus_unlock(b);
    return rc;
}

/**
 * @brief 开始硬件数据流
 * @param dev 设备句柄
 * @param xfer 事务句柄
 * @return 32位状态码
 */
ret_code_t hal_spi_stream_start(hal_spi_dev_t *dev, const hal_spi_xfer_t *xfer) {
    ret_code_t rc = validate_api_xfer_common(dev, xfer, true);
    if (ret_is_err(rc)) return rc;
    hal_spi_bus_t *b = dev->bus;
    /* 锁住总线 */
    rc               = bus_lock(b, xfer->timeout_ms ? xfer->timeout_ms : OSAL_WAIT_FOREVER);
    if (ret_is_err(rc)) return spi_map_runtime_to_hal(rc);
    /* 开始通信 */
    rc = spi_stream_start_guarded(dev, xfer);
    /* 解锁 */
    bus_unlock(b);
    return rc;
}
/**
 *
 * @param dev 设备句柄
 * @param disable_spi 是否关闭SPI
 * @return
 */
ret_code_t hal_spi_stream_stop(hal_spi_dev_t *dev, bool disable_spi) {
    REQUIRE_RET(dev != NULL, SPI_RC_PARAM(RET_R_NULL_PTR));
    if (!dev->in_use || (dev->bus == NULL) || !dev->bus->initialized)
        return SPI_RC_STATE(RET_R_NOT_READY);
    hal_spi_bus_t *b = dev->bus;
    /* 锁 */
    ret_code_t rc    = bus_lock(b, OSAL_WAIT_FOREVER);
    if (ret_is_err(rc)) return spi_map_runtime_to_hal(rc);
    rc = spi_stream_stop_guarded(dev, disable_spi);
    /* 解锁 */
    bus_unlock(b);
    return rc;
}

#endif
