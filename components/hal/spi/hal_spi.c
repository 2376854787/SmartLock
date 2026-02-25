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

#include "assert_cus.h"
#include "hal_gpio.h"
#include "hal_spi.h"
#include "hal_spi_port.h"
#include "hal_time.h"
#include "osal.h"
#if defined(CFG_FEAT_LOG_SYSTEM) && (CFG_FEAT_LOG_SYSTEM == 1)
#include "log.h"
#endif
#ifndef CFG_PARAM_SPI_EVT_DISPATCH_EVENTBUS
#define CFG_PARAM_SPI_EVT_DISPATCH_EVENTBUS 0
#endif
#ifndef CFG_PARAM_SPI_EVT_DISPATCH_QUEUE
#define CFG_PARAM_SPI_EVT_DISPATCH_QUEUE 1
#endif
#ifndef CFG_PARAM_SPI_EVT_DISPATCH_TASK_NOTIFY
#define CFG_PARAM_SPI_EVT_DISPATCH_TASK_NOTIFY 2
#endif
#ifndef CFG_PARAM_SPI_EVT_DISPATCH_MODE
#define CFG_PARAM_SPI_EVT_DISPATCH_MODE CFG_PARAM_SPI_EVT_DISPATCH_EVENTBUS
#endif
#ifndef CFG_PARAM_SPI_EVT_NOTIFY_FLAG_DONE
#define CFG_PARAM_SPI_EVT_NOTIFY_FLAG_DONE (1u << 0)
#endif
#ifndef CFG_PARAM_SPI_EVT_NOTIFY_FLAG_ERROR
#define CFG_PARAM_SPI_EVT_NOTIFY_FLAG_ERROR (1u << 1)
#endif
#ifndef CFG_PARAM_SPI_EVT_NOTIFY_FLAG_STREAM_HALF
#define CFG_PARAM_SPI_EVT_NOTIFY_FLAG_STREAM_HALF (1u << 2)
#endif
#ifndef CFG_PARAM_SPI_EVT_NOTIFY_FLAG_STREAM_FULL
#define CFG_PARAM_SPI_EVT_NOTIFY_FLAG_STREAM_FULL (1u << 3)
#endif

#if (CFG_PARAM_SPI_EVT_DISPATCH_MODE == CFG_PARAM_SPI_EVT_DISPATCH_EVENTBUS)
#include "eb_api.h"
#include "eb_event_id.h"
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
    bool initialized;        /* 判断当前总线是否已经初始化 */
    hal_spi_bus_cfg_t cfg;   /* 总线配置 DMA irq 默认速率 */
    osal_mutex_t lock;       /* 互斥锁 */
    bool lock_valid;         /* 判断当前是否是启动了RTOS 环境 */
    osal_sem_t sync_sem;     /* 同步事务完成信号量 */
    bool sync_sem_valid;     /* 同步信号量是否可用 */
    hal_spi_port_ctx_t port; /* 板级资源 */

    volatile uint8_t xfer_busy; /* 当前是否有异步事务在进行 */
    hal_spi_dev_t *active_dev;  /* 当前活跃设备 */
    uint32_t active_flags;      /* 当前事务 flags（KEEP_CS/NO_CS/STREAM/HW_STREAM） */
    /* ======= 同步事务资源 =======*/
    volatile uint8_t sync_waiting; /* 当前是否存在同步等待方 */
    volatile uint8_t sync_done;    /* 同步事务是否完成 */
    ret_code_t sync_rc;            /* 同步事务完成码 */
    uint32_t sync_bytes;           /* 同步事务完成字节数 */
};
/* 设备 */
struct hal_spi_dev {
    bool in_use;           /* 设备是否被挂载 */
    hal_spi_bus_t *bus;    /* 总线配置 */
    hal_spi_dev_cfg_t cfg; /* 设备配置 */
    hal_gpio_t *cs;        /* 片选线句柄 */
    bool cs_valid;         /* 是否可以手动翻转片选线 */

    hal_spi_evt_cb_t evt_cb; /* 异步事件回调 */
    void *evt_user;          /* 回调用户上下文 */
    void *evt_target;        /* 事件分发目标（queue 或 thread） */
};

/* 总线资源 */
static struct hal_spi_bus s_buses[HAL_SPI_BUS_MAX];
/* 设备资源 */
static struct hal_spi_dev s_devs[HAL_SPI_DEV_MAX];

#if (CFG_PARAM_SPI_EVT_DISPATCH_MODE == CFG_PARAM_SPI_EVT_DISPATCH_EVENTBUS)
/**
 * @brief 将 SPI 设备上下文编码为 eventbus key
 * @note key布局：[31:24]bus_id [23:16]cs_type [15:0]cs_gpio_id(低16位)
 */
static uint32_t spi_evt_make_key(const hal_spi_dev_t *d) {
    if (!d || !d->bus) return 0u;
    const uint32_t bus_id = (uint32_t)d->bus->cfg.bus_id & 0xFFu;
    const uint32_t cs_t   = (uint32_t)d->cfg.cs_type & 0xFFu;
    const uint32_t cs_id  = (uint32_t)d->cfg.cs_gpio_id & 0xFFFFu;
    return (bus_id << 24) | (cs_t << 16) | cs_id;
}

/**
 * @brief 将 HAL SPI 事件投递到 eventbus
 * @param d   设备句柄
 * @param evt 事件载体
 * @note 投递失败不影响 SPI 事务主流程
 */
static void spi_publish_eventbus(const hal_spi_dev_t *d, const hal_spi_event_t *evt) {
    if (!d || !d->bus || !evt) return;

    uint32_t event_id = 0u;
    uint32_t payload  = 0u;
    switch (evt->type) {
        case HAL_SPI_EVT_DONE:
            event_id = EB_EVT_SPI_DONE;
            payload  = evt->done.bytes;
            break;
        case HAL_SPI_EVT_ERROR:
            event_id = EB_EVT_SPI_ERROR;
            payload  = (uint32_t)evt->err.rc;
            break;
        case HAL_SPI_EVT_STREAM_HALF:
            event_id = EB_EVT_SPI_STREAM_HALF;
            payload  = evt->stream.bytes;
            break;
        case HAL_SPI_EVT_STREAM_FULL:
            event_id = EB_EVT_SPI_STREAM_FULL;
            payload  = evt->stream.bytes;
            break;
        default:
            return;
    }

    const eb_event_t ev = {
        .event_id    = event_id,
        .prio        = 0, /* 由 eventdef 强制覆盖 */
        .key         = spi_evt_make_key(d),
        .payload_u32 = payload,
        .source_id   = (uint16_t)d->bus->cfg.bus_id,
        .type_tag    = 0u,
    };
    (void)eb_publish(&ev);
}
#endif

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

/**
 * @brief 判断当前总线是否仍有设备挂载
 * @param bus 总线句柄
 * @return true: 仍有设备，false: 无设备
 */
static bool bus_has_attached_dev(const hal_spi_bus_t *bus) {
    if (!bus) return false;
    osal_crit_state_t cs = 0u;
    bool found           = false;
    OSAL_enter_critical_ex(&cs);
    for (uint32_t i = 0; i < HAL_SPI_DEV_MAX; i++) {
        if (s_devs[i].in_use && (s_devs[i].bus == bus)) {
            found = true;
            break;
        }
    }
    OSAL_exit_critical_ex(cs);
    return found;
}

static hal_spi_dev_t *reserve_dev_slot(hal_spi_bus_t *bus, const hal_spi_dev_cfg_t *cfg) {
    if (!bus || !cfg) return NULL;

    hal_spi_dev_t *d     = NULL;
    osal_crit_state_t cs = 0u;
    OSAL_enter_critical_ex(&cs);
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
    OSAL_exit_critical_ex(cs);
    return d;
}

static void release_dev_slot(hal_spi_dev_t *d) {
    if (!d) return;
    osal_crit_state_t cs = 0u;
    OSAL_enter_critical_ex(&cs);
    memset(d, 0, sizeof(*d));
    OSAL_exit_critical_ex(cs);
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
    return OSAL_mutex_lock(b->lock, timeout_ms);
}
/**
 * @brief 总线解锁
 * @param b 总线句柄
 */
static void bus_unlock(hal_spi_bus_t *b) {
    CORE_ASSERT(b != NULL);
    if (!b) return;
    if (!b->lock_valid) return;
    (void)OSAL_mutex_unlock(b->lock);
}
/**
 * @brief 将 SPI 事件类型映射到 task-notify flags
 * @param type SPI 事件类型
 * @return flags 掩码；0 表示不通知
 */
#if (CFG_PARAM_SPI_EVT_DISPATCH_MODE == CFG_PARAM_SPI_EVT_DISPATCH_TASK_NOTIFY)
static inline osal_flags_t spi_evt_to_notify_flags(hal_spi_evt_type_t type) {
    switch (type) {
        case HAL_SPI_EVT_DONE:
            return (osal_flags_t)CFG_PARAM_SPI_EVT_NOTIFY_FLAG_DONE;
        case HAL_SPI_EVT_ERROR:
            return (osal_flags_t)CFG_PARAM_SPI_EVT_NOTIFY_FLAG_ERROR;
        case HAL_SPI_EVT_STREAM_HALF:
            return (osal_flags_t)CFG_PARAM_SPI_EVT_NOTIFY_FLAG_STREAM_HALF;
        case HAL_SPI_EVT_STREAM_FULL:
            return (osal_flags_t)CFG_PARAM_SPI_EVT_NOTIFY_FLAG_STREAM_FULL;
        default:
            return 0u;
    }
}
#endif

/**
 * @brief SPI 设备事件分发
 * @param d 设备句柄
 * @param evt 事件载体
 */
static inline void emit_dev_evt(const hal_spi_dev_t *d, const hal_spi_event_t *evt) {
    if (!d || !evt) return;
#if (CFG_PARAM_SPI_EVT_DISPATCH_MODE == CFG_PARAM_SPI_EVT_DISPATCH_EVENTBUS)
    spi_publish_eventbus(d, evt);
#elif (CFG_PARAM_SPI_EVT_DISPATCH_MODE == CFG_PARAM_SPI_EVT_DISPATCH_QUEUE)
    if (d->evt_target != NULL) {
        hal_spi_event_t out_evt = *evt;
        (void)OSAL_msgq_put((osal_msgq_t)d->evt_target, (void *)&out_evt, 0u);
    }
#elif (CFG_PARAM_SPI_EVT_DISPATCH_MODE == CFG_PARAM_SPI_EVT_DISPATCH_TASK_NOTIFY)
    if (d->evt_target != NULL) {
        const osal_flags_t flags = spi_evt_to_notify_flags(evt->type);
        if (flags != 0u) {
            (void)OSAL_thread_flags_set((osal_thread_t)d->evt_target, flags);
        }
    }
#endif

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
 *
 * @param dev
 * @param xfer
 * @param hw_stream_api
 * @return
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
    osal_crit_state_t cs = 0u;
    OSAL_enter_critical_ex(&cs);
    if (b->xfer_busy) {
        OSAL_exit_critical_ex(cs);
        return SPI_RC_STATE(RET_R_BUSY);
    }
    b->xfer_busy    = 1u;
    b->active_dev   = d;
    b->active_flags = flags;
    OSAL_exit_critical_ex(cs);
    return RET_OK;
}

/**
 * @brief 原子释放总线当前事务槽位
 * @param b 总线句柄
 */
static void bus_release_active_xfer(hal_spi_bus_t *b) {
    if (!b) return;
    osal_crit_state_t cs = 0u;
    OSAL_enter_critical_ex(&cs);
    b->xfer_busy    = 0u;
    b->active_dev   = NULL;
    b->active_flags = 0u;
    OSAL_exit_critical_ex(cs);
}

/**
 * @brief 将等待类错误映射为 HAL SPI 统一错误码
 * @param rc 等待接口返回值（如 OSAL_sem_take）
 * @return HAL SPI 统一错误码
 */
static ret_code_t spi_map_wait_rc_to_hal(ret_code_t rc) {
    if (ret_is_ok(rc)) return RET_OK;

    if (ret_is_class(rc, RET_CLASS_PARAM)) {
        if (ret_is_reason(rc, RET_R_NULL_PTR))
            return SPI_RC_PARAM(RET_R_NULL_PTR);
        else if (ret_is_reason(rc, RET_R_RANGE_ERR))
            return SPI_RC_PARAM(RET_R_RANGE_ERR);
        else
            return SPI_RC_PARAM(RET_R_INVALID_ARG);
    }

    if (ret_is_class(rc, RET_CLASS_TIMEOUT)) return SPI_RC_TIMEOUT(RET_R_TIMEOUT);

    if (ret_is_class(rc, RET_CLASS_RESOURCE)) {
        if (ret_is_reason(rc, RET_R_NO_MEM))
            return SPI_RC_RES(RET_R_NO_MEM);
        else
            return SPI_RC_RES(RET_R_NO_RESOURCE);
    }

    if (ret_is_class(rc, RET_CLASS_STATE)) {
        if (ret_is_reason(rc, RET_R_BUSY))
            return SPI_RC_STATE(RET_R_BUSY);
        else if (ret_is_reason(rc, RET_R_NOT_READY))
            return SPI_RC_STATE(RET_R_NOT_READY);
        else
            return SPI_RC_STATE(RET_R_STATE_ERR);
    }

    return SPI_RC_IO(RET_R_IO);
}

/**
 * @brief 规范化同步等待超时参数
 * @param wait_ms 用户输入等待时间（ms）
 * @return 实际等待时间；0 被转换为 OSAL_WAIT_FOREVER
 */
static inline uint32_t spi_sync_norm_wait(uint32_t wait_ms) {
    return (wait_ms == 0u) ? OSAL_WAIT_FOREVER : wait_ms;
}

/**
 * @brief 清空同步信号量中历史残留计数
 * @param sem 同步信号量
 */
static void spi_sync_sem_drain(osal_sem_t sem) {
    if (!sem) return;
    while (OSAL_sem_take(sem, 0u) == RET_OK) {
        /* drain */
    }
}

/**
 * @brief 准备一次新的同步等待上下文
 * @param b 总线句柄
 * @note 会重置同步完成状态，并清理旧的信号量计数
 */
static void spi_sync_prepare_wait(hal_spi_bus_t *b) {
    if (!b) return;
    if (b->sync_sem_valid) spi_sync_sem_drain(b->sync_sem);

    osal_crit_state_t cs = 0u;
    OSAL_enter_critical_ex(&cs);
    b->sync_waiting = 1u;
    b->sync_done    = 0u;
    b->sync_rc      = SPI_RC_STATE(RET_R_STATE_ERR);
    b->sync_bytes   = 0u;
    OSAL_exit_critical_ex(cs);
}

/**
 * @brief 终止当前同步等待状态
 * @param b 总线句柄
 * @note 用于发起失败或等待失败后的收敛清理
 */
static void spi_sync_abort_wait(hal_spi_bus_t *b) {
    if (!b) return;
    osal_crit_state_t cs = 0u;
    OSAL_enter_critical_ex(&cs);
    b->sync_waiting = 0u;
    b->sync_done    = 0u;
    b->sync_rc      = SPI_RC_STATE(RET_R_STATE_ERR);
    b->sync_bytes   = 0u;
    OSAL_exit_critical_ex(cs);
}

/**
 * @brief 标记同步事务完成并唤醒等待方
 * @param b      总线句柄
 * @param rc_hal 完成结果（HAL 语义）
 * @param bytes  完成字节数
 */
static void spi_sync_mark_done(hal_spi_bus_t *b, ret_code_t rc_hal, uint32_t bytes) {
    if (!b) return;

    bool need_notify     = false;
    osal_crit_state_t cs = 0u;
    OSAL_enter_critical_ex(&cs);
    if (b->sync_waiting) {
        b->sync_waiting = 0u;
        b->sync_done    = 1u;
        b->sync_rc      = rc_hal;
        b->sync_bytes   = bytes;
        need_notify     = b->sync_sem_valid;
    }
    OSAL_exit_critical_ex(cs);

    if (need_notify) {
        if (OSAL_in_isr())
            (void)OSAL_sem_give_from_isr(b->sync_sem);
        else
            (void)OSAL_sem_give(b->sync_sem);
    }
}

/**
 * @brief 等待同步事务完成（信号量或轮询）
 * @param b         总线句柄
 * @param wait_ms   等待超时（ms）
 * @param done_bytes 返回完成字节数，可为 NULL
 * @return RET_OK 或统一错误码
 */
static ret_code_t spi_sync_wait_done(hal_spi_bus_t *b, uint32_t wait_ms, uint32_t *done_bytes) {
    if (!b) return SPI_RC_PARAM(RET_R_NULL_PTR);

    const uint32_t wait_eff = spi_sync_norm_wait(wait_ms);
    if (b->sync_sem_valid) {
        const ret_code_t sem_rc = OSAL_sem_take(b->sync_sem, wait_eff);
        if (ret_is_err(sem_rc)) {
            spi_sync_abort_wait(b);
            return spi_map_wait_rc_to_hal(sem_rc);
        }
    } else {
        const uint32_t deadline_ms =
            (wait_eff == OSAL_WAIT_FOREVER) ? 0u : (hal_get_tick_ms() + wait_eff);
        while (1) {
            bool done            = false;
            osal_crit_state_t cs = 0u;
            OSAL_enter_critical_ex(&cs);
            done = (b->sync_done != 0u);
            OSAL_exit_critical_ex(cs);
            if (done) break;

            if ((wait_eff != OSAL_WAIT_FOREVER) &&
                HAL_TIME_AFTER_EQ(hal_get_tick_ms(), deadline_ms)) {
                spi_sync_abort_wait(b);
                return SPI_RC_TIMEOUT(RET_R_TIMEOUT);
            }

            if (OSAL_kernel_is_running()) {
                (void)OSAL_delay_ms(1u);
            }
        }
    }

    ret_code_t rc_hal    = SPI_RC_STATE(RET_R_STATE_ERR);
    osal_crit_state_t cs = 0u;
    OSAL_enter_critical_ex(&cs);
    if (b->sync_done) {
        rc_hal = b->sync_rc;
        if (done_bytes) *done_bytes = b->sync_bytes;
        b->sync_waiting = 0u;
        b->sync_done    = 0u;
        b->sync_bytes   = 0u;
    } else {
        b->sync_waiting = 0u;
        b->sync_done    = 0u;
        b->sync_rc      = SPI_RC_STATE(RET_R_STATE_ERR);
        b->sync_bytes   = 0u;
    }
    OSAL_exit_critical_ex(cs);
    return rc_hal;
}

/**
 * @brief port 层事件回调
 * @param user 总线句柄
 * @param evt 事件载体
 */
static void spi_port_evt_cb(void *user, const hal_spi_port_evt_t *evt) {
    hal_spi_bus_t *b = (hal_spi_bus_t *)user;
    if (!b || !b->initialized || !evt) return;

    hal_spi_dev_t *dev   = NULL;
    uint32_t flags       = 0u;

    /* 原子读取当前活跃事务（先不清 busy，防止并发 detach 抢占） */
    osal_crit_state_t cs = 0u;
    OSAL_enter_critical_ex(&cs);
    dev   = b->active_dev;
    flags = b->active_flags;
    OSAL_exit_critical_ex(cs);

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

    /* 如果当前有同步等待方，写入完成态并尝试唤醒 */
    spi_sync_mark_done(b, rc_hal, bytes);

    /* 上报事件 给用户回调函数 */
    emit_dev_evt(dev, &hevt);
}

/**
 * @brief 获取 SPI 抽象句柄
 * @param cfg 总线配置
 * @param out_bus 返回板级的映射配置
 * @return 32位状态码
 */
ret_code_t hal_spi_bus_open(const hal_spi_bus_cfg_t *cfg, hal_spi_bus_t **out_bus) {
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
    rc             = hal_spi_port_open(cfg, &b->port);
    if (ret_is_err(rc)) {
        b->initialized = false;
        return spi_map_port_to_hal(rc, "hal_spi_port_open", cfg->bus_id, cfg->default_hz);
    }
    /* 注册 port 异步完成回调 */
    rc = hal_spi_port_set_evt_cb(&b->port, spi_port_evt_cb, b);
    if (ret_is_err(rc)) {
        (void)hal_spi_port_close(&b->port);
        b->initialized = false;
        return spi_map_port_to_hal(rc, "hal_spi_port_set_evt_cb", cfg->bus_id, 0u);
    }

    /* RTOS 环境下创建互斥锁 */
    if (OSAL_kernel_is_running()) {
        if (ret_is_ok(OSAL_mutex_create(&b->lock, "spi_bus", false, true))) {
            b->lock_valid = true;
        }
        if (ret_is_ok(OSAL_sem_create(&b->sync_sem, "spi_sync", 0u, 1u))) {
            b->sync_sem_valid = true;
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
ret_code_t hal_spi_bus_close(hal_spi_bus_t *bus) {
    /* 参数检查 */
    REQUIRE_RET(bus != NULL, SPI_RC_PARAM(RET_R_NULL_PTR));
    if (!bus->initialized) return SPI_RC_STATE(RET_R_NOT_READY);
    /* 异步传输进行中不允许关闭总线 */
    if (bus->xfer_busy) return SPI_RC_STATE(RET_R_BUSY);
    /* 如果仍有设备挂在当前总线上，拒绝关闭总线 */
    if (bus_has_attached_dev(bus)) return SPI_RC_STATE(RET_R_BUSY);

    /* 释放资源 SPI句柄、DMA、*/
    const ret_code_t rc = hal_spi_port_close(&bus->port);
    if (ret_is_err(rc)) return spi_map_port_to_hal(rc, "hal_spi_port_close", bus->cfg.bus_id, 0u);

    /* 有互斥锁就删除掉 */
    if (bus->lock_valid) {
        (void)OSAL_mutex_delete(bus->lock);
        bus->lock_valid = false;
    }
    if (bus->sync_sem_valid) {
        (void)OSAL_sem_delete(bus->sync_sem);
        bus->sync_sem_valid = false;
    }
    bus->initialized = false;
    return RET_OK;
}
/**
 * @brief 初始化设备句柄并将设备和总线进行绑定注册
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

    hal_spi_dev_t *d = reserve_dev_slot(bus, cfg);
    if (!d) return SPI_RC_RES(RET_R_NO_RESOURCE);

    /* 软件片选线 类型配置 */
    if (cfg->cs_type == HAL_SPI_CS_GPIO) {
        rc = hal_gpio_open(&d->cs, cfg->cs_gpio_id);
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
            (void)hal_gpio_close(d->cs);
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
 * @brief 注册 SPI 事件分发目标句柄
 * @param dev 设备句柄
 * @param target 分发目标（queue 模式=msgq，task-notify 模式=thread）
 * @return 32位状态码
 */
ret_code_t hal_spi_dev_set_evt_target(hal_spi_dev_t *dev, void *target) {
    REQUIRE_RET(dev != NULL, SPI_RC_PARAM(RET_R_NULL_PTR));
    if (!dev->in_use) return SPI_RC_STATE(RET_R_NOT_READY);
    dev->evt_target = target;
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
        bool busy_active     = false;
        osal_crit_state_t cs = 0u;
        OSAL_enter_critical_ex(&cs);
        /* 没有异步事物正在进行 且 当前解绑的设备不能是活跃设备 需要等回调调用
         * bus_release_active_xfer 释放事务*/
        busy_active = (dev->bus->xfer_busy != 0u) && (dev->bus->active_dev == dev);
        OSAL_exit_critical_ex(cs);
        if (busy_active) return SPI_RC_STATE(RET_R_BUSY);
    }
    if (dev->cs_valid) {
        (void)hal_gpio_close(dev->cs);
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
    /* 原子申请事务 */
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

    const uint32_t flags = b->active_flags;
    const ret_code_t rc  = hal_spi_port_abort(&b->port, disable_spi);
    if (ret_is_err(rc)) {
        return spi_map_port_to_hal(rc, "hal_spi_port_abort", b->cfg.bus_id, disable_spi ? 1u : 0u);
    }

    const bool no_cs = (flags & HAL_SPI_XFER_NO_CS) != 0u;
    if (!no_cs) cs_deassert(d);
    bus_release_active_xfer(b);

    /* 中止属于业务中断当前事务，向回调与同步等待统一上报 ERROR。 */
    const ret_code_t abort_rc = SPI_RC_STATE(RET_R_ABORTED);
    spi_sync_mark_done(b, abort_rc, 0u);

    hal_spi_event_t evt = {0};
    evt.type            = HAL_SPI_EVT_ERROR;
    evt.err.rc          = abort_rc;
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
    if (ret_is_err(rc)) return rc;
    /* 更新配置并发起异步传输（发起成功即返回） */
    rc = spi_xfer_guarded(dev, xfer);
    bus_unlock(b);
    return rc;
}

/**
 * @brief 同步收发通用实现（基于异步发起 + 完成等待）
 * @param dev     设备句柄
 * @param xfer    一次性事务参数
 * @param wait_ms 完成等待超时（ms）
 * @return RET_OK 或统一错误码
 * @note 该函数只用于一次性事务，不处理 stream 事务
 */
static ret_code_t spi_transceive_sync_common(hal_spi_dev_t *dev, const hal_spi_xfer_t *xfer,
                                             uint32_t wait_ms) {
    REQUIRE_RET(!OSAL_in_isr(), SPI_RC_STATE(RET_R_STATE_ERR));

    ret_code_t rc = validate_api_xfer_common(dev, xfer, false);
    if (ret_is_err(rc)) return rc;
    hal_spi_bus_t *b      = dev->bus;
    const uint32_t wait_t = spi_sync_norm_wait(wait_ms);

    uint32_t deadline_ms  = 0u;
    if (wait_t != OSAL_WAIT_FOREVER) deadline_ms = hal_get_tick_ms() + wait_t;

    rc = bus_lock(b, wait_t);
    if (ret_is_err(rc)) return rc;

    spi_sync_prepare_wait(b);
    rc = spi_xfer_guarded(dev, xfer);
    bus_unlock(b);

    if (ret_is_err(rc)) {
        spi_sync_abort_wait(b);
        return rc;
    }

    uint32_t remain_ms = wait_t;
    if (wait_t != OSAL_WAIT_FOREVER) {
        const uint32_t now_ms = hal_get_tick_ms();
        if (HAL_TIME_AFTER_EQ(now_ms, deadline_ms))
            remain_ms = 0u;
        else
            remain_ms = deadline_ms - now_ms;
    }

    return spi_sync_wait_done(b, remain_ms, NULL);
}

/**
 * @brief 同步收发（阻塞直到完成或超时）
 * @param dev 设备句柄
 * @param tx  发送缓存，可为 NULL（纯接收）
 * @param rx  接收缓存，可为 NULL（纯发送）
 * @param len 传输长度（字节）
 * @param wait_ms 等待完成超时（ms），0 表示永久等待
 * @return RET_OK 或错误码
 */
ret_code_t hal_spi_transceive_sync(hal_spi_dev_t *dev, const void *tx, void *rx, uint32_t len,
                                   uint32_t wait_ms) {
    const hal_spi_xfer_t xfer = {
        .tx         = tx,
        .rx         = rx,
        .len        = len,
        .timeout_ms = wait_ms,
        .flags      = HAL_SPI_XFER_NONE,
    };
    return spi_transceive_sync_common(dev, &xfer, wait_ms);
}

/**
 * @brief 同步发送
 * @param dev 设备句柄
 * @param tx  发送缓存地址
 * @param len 发送长度（字节）
 * @param wait_ms 等待完成超时（ms），0 表示永久等待
 * @return RET_OK 或错误码
 */
ret_code_t hal_spi_send_sync(hal_spi_dev_t *dev, const void *tx, uint32_t len, uint32_t wait_ms) {
    REQUIRE_RET(tx != NULL, SPI_RC_PARAM(RET_R_NULL_PTR));
    return hal_spi_transceive_sync(dev, tx, NULL, len, wait_ms);
}

/**
 * @brief 同步接收
 * @param dev 设备句柄
 * @param rx  接收缓存地址
 * @param len 接收长度（字节）
 * @param wait_ms 等待完成超时（ms），0 表示永久等待
 * @return RET_OK 或错误码
 */
ret_code_t hal_spi_recv_sync(hal_spi_dev_t *dev, void *rx, uint32_t len, uint32_t wait_ms) {
    REQUIRE_RET(rx != NULL, SPI_RC_PARAM(RET_R_NULL_PTR));
    return hal_spi_transceive_sync(dev, NULL, rx, len, wait_ms);
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
    if (ret_is_err(rc)) return rc;

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
    if (ret_is_err(rc)) return rc;
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
    if (ret_is_err(rc)) return rc;
    rc = spi_stream_stop_guarded(dev, disable_spi);
    /* 解锁 */
    bus_unlock(b);
    return rc;
}

#endif
