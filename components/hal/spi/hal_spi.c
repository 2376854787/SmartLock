#include "APP_config.h"
#if defined(CFG_FEAT_HAL_SPI) && (CFG_FEAT_HAL_SPI == 1)

#include <limits.h>
#include <string.h>

#include "hal_gpio.h"
#include "hal_spi.h"
#include "hal_spi_port.h"
#include "hal_time.h"
#include "osal.h"

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
    hal_spi_port_ctx_t port; /* 板级资源 */

    volatile uint8_t xfer_busy; /* 当前是否有异步事务在进行 */
    hal_spi_dev_t *active_dev;  /* 当前活跃设备 */
    uint32_t active_flags;      /* 当前事务 flags（KEEP_CS/NO_CS/STREAM/HW_STREAM） */
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
static inline ret_code_t spi_map_port_to_hal(ret_code_t rc_port) {
    if (ret_is_ok(rc_port)) return RET_OK;

    if (ret_is_class(rc_port, RET_CLASS_PARAM)) {
        if (ret_is_reason(rc_port, RET_R_NULL_PTR)) return SPI_RC_PARAM(RET_R_NULL_PTR);
        if (ret_is_reason(rc_port, RET_R_RANGE_ERR)) return SPI_RC_PARAM(RET_R_RANGE_ERR);
        if (ret_is_reason(rc_port, RET_R_UNSUPPORTED)) return SPI_RC_PARAM(RET_R_UNSUPPORTED);
        return SPI_RC_PARAM(RET_R_INVALID_ARG);
    }

    if (ret_is_class(rc_port, RET_CLASS_TIMEOUT)) return SPI_RC_TIMEOUT(RET_R_TIMEOUT);

    if (ret_is_class(rc_port, RET_CLASS_RESOURCE)) {
        if (ret_is_reason(rc_port, RET_R_NO_MEM)) return SPI_RC_RES(RET_R_NO_MEM);
        return SPI_RC_RES(RET_R_NO_RESOURCE);
    }

    if (ret_is_class(rc_port, RET_CLASS_STATE)) {
        if (ret_is_reason(rc_port, RET_R_BUSY)) return SPI_RC_STATE(RET_R_BUSY);
        if (ret_is_reason(rc_port, RET_R_NOT_READY)) return SPI_RC_STATE(RET_R_NOT_READY);
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
    if (!b->lock_valid) return RET_OK;
    return OSAL_mutex_lock(b->lock, timeout_ms);
}
/**
 * @brief 总线解锁
 * @param b 总线句柄
 */
static void bus_unlock(hal_spi_bus_t *b) {
    if (!b->lock_valid) return;
    (void)OSAL_mutex_unlock(b->lock);
}
/**
 * @brief SPI 设备事件分发
 * @param d 设备句柄
 * @param evt 事件载体
 */
static inline void emit_dev_evt(const hal_spi_dev_t *d, const hal_spi_event_t *evt) {
    if (d && d->evt_cb) d->evt_cb(d->evt_user, evt);
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

static void wait_spi_idle_before_cs_deassert(const hal_spi_dev_t *d) {
#if defined(CFG_PARAM_SPI_CS_DEASSERT_WAIT_BSY) && (CFG_PARAM_SPI_CS_DEASSERT_WAIT_BSY == 1) && \
    defined(SPI_FLAG_BSY)
    if (!d || !d->cs_valid || !d->bus) return;
    SPI_HandleTypeDef *hspi = d->bus->port.bsp.hspi;
    if (!hspi) return;

    uint32_t spin = (uint32_t)CFG_PARAM_SPI_CS_DEASSERT_WAIT_BSY_SPIN_MAX;
    while ((__HAL_SPI_GET_FLAG(hspi, SPI_FLAG_BSY) != RESET) && (spin > 0u)) {
        spin--;
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
    if (!cfg) return SPI_RC_PARAM(RET_R_NULL_PTR);
    /* 检查 bus_id 是否在有效范围内 */
    if (cfg->bus_id >= HAL_SPI_BUS_MAX) return SPI_RC_PARAM(RET_R_RANGE_ERR);
    /* 确保合法速率 */
    if (cfg->default_hz == 0u) return SPI_RC_PARAM(RET_R_RANGE_ERR);
    return RET_OK;
}
/**
 * @brief 检查 SPI 设备的配置
 * @param cfg 设备句柄
 * @return
 */
static ret_code_t cfg_check_dev(const hal_spi_dev_cfg_t *cfg) {
    /* 非 NULL */
    if (!cfg) return SPI_RC_PARAM(RET_R_NULL_PTR);
    /* 模式合法检查 */
    if (cfg->mode > HAL_SPI_MODE3) return SPI_RC_PARAM(RET_R_INVALID_ARG);
    /* 大小端合法检查 */
    if (cfg->bit_order != HAL_SPI_BITORDER_MSB && cfg->bit_order != HAL_SPI_BITORDER_LSB)
        return SPI_RC_PARAM(RET_R_INVALID_ARG);
    /* 最大速率 大于0 */
    if (cfg->max_hz == 0u) return SPI_RC_PARAM(RET_R_RANGE_ERR);
    /* 位宽合法检查 */
    if (cfg->frame_bits != HAL_SPI_FRAME_8 && cfg->frame_bits != HAL_SPI_FRAME_16)
        return SPI_RC_PARAM(RET_R_INVALID_ARG);
    /* 双工方向合法检查 */
    if (cfg->dir != HAL_SPI_DIR_2LINES && cfg->dir != HAL_SPI_DIR_2LINES_RXONLY &&
        cfg->dir != HAL_SPI_DIR_LINE)
        return SPI_RC_PARAM(RET_R_INVALID_ARG);
    /* 片选类型合法检查 */
    if (cfg->cs_type != HAL_SPI_CS_GPIO && cfg->cs_type != HAL_SPI_CS_HW)
        return SPI_RC_PARAM(RET_R_INVALID_ARG);
    if (cfg->cs_type == HAL_SPI_CS_GPIO) {
        if (cfg->cs_gpio_id == 0u) return SPI_RC_PARAM(RET_R_RANGE_ERR);
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
    if (!dev || !xfer) return SPI_RC_PARAM(RET_R_NULL_PTR);
    /* 必须设备被使用和以及绑定的总线被初始化 */
    if (!dev->in_use || !dev->bus || !dev->bus->initialized) return SPI_RC_STATE(RET_R_NOT_READY);
    /* 长度必须有效 */
    if (xfer->len == 0u) return SPI_RC_PARAM(RET_R_RANGE_ERR);
    /* 发送和接收地址不能同时为空 */
    if (xfer->tx == NULL && xfer->rx == NULL) return SPI_RC_PARAM(RET_R_INVALID_ARG);
    /* 硬件流api调用必查看是不是 end事务*/
    if (hw_stream_api) {
        if (xfer->flags & HAL_SPI_XFER_STREAM_END) return SPI_RC_PARAM(RET_R_INVALID_ARG);
    } else {
        /* 非硬件api 就不能有硬件流事务*/
        if (xfer->flags & HAL_SPI_XFER_HW_STREAM) return SPI_RC_PARAM(RET_R_UNSUPPORTED);
        /* 非硬件流 且是流结束事务 && 不是软件流 返回错误 */
        if ((xfer->flags & HAL_SPI_XFER_STREAM_END) && !(xfer->flags & HAL_SPI_XFER_STREAM))
            return SPI_RC_PARAM(RET_R_INVALID_ARG);
    }
    /* 检查位宽和发送的长度是否对的上 */
    const uint32_t frame_bytes = (dev->cfg.frame_bits == HAL_SPI_FRAME_16) ? 2u : 1u;
    if ((xfer->len % frame_bytes) != 0u) return SPI_RC_PARAM(RET_R_RANGE_ERR);
    if ((xfer->len / frame_bytes) > (uint32_t)UINT16_MAX) return SPI_RC_PARAM(RET_R_RANGE_ERR);

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
    if (ret_is_ok(rc_port)) {
        hevt.type       = HAL_SPI_EVT_DONE;
        hevt.done.bytes = bytes;
    } else {
        hevt.type   = HAL_SPI_EVT_ERROR;
        hevt.err.rc = spi_map_port_to_hal(rc_port);
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
ret_code_t hal_spi_bus_open(const hal_spi_bus_cfg_t *cfg, hal_spi_bus_t **out_bus) {
    /* 参数检查 */
    if (!out_bus) return SPI_RC_PARAM(RET_R_NULL_PTR);
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
        return spi_map_port_to_hal(rc);
    }
    /* 注册 port 异步完成回调 */
    rc = hal_spi_port_set_evt_cb(&b->port, spi_port_evt_cb, b);
    if (ret_is_err(rc)) {
        (void)hal_spi_port_close(&b->port);
        b->initialized = false;
        return spi_map_port_to_hal(rc);
    }

    /* RTOS 环境下创建互斥锁 */
    if (OSAL_kernel_is_running()) {
        if (ret_is_ok(OSAL_mutex_create(&b->lock, "spi_bus", false, true))) {
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
ret_code_t hal_spi_bus_close(hal_spi_bus_t *bus) {
    /* 参数检查 */
    if (!bus) return SPI_RC_PARAM(RET_R_NULL_PTR);
    if (!bus->initialized) return SPI_RC_STATE(RET_R_NOT_READY);
    /* 异步传输进行中不允许关闭总线 */
    if (bus->xfer_busy) return SPI_RC_STATE(RET_R_BUSY);
    /* 如果仍有设备挂在当前总线上，拒绝关闭总线 */
    if (bus_has_attached_dev(bus)) return SPI_RC_STATE(RET_R_BUSY);

    /* 释放资源 SPI句柄、DMA、*/
    const ret_code_t rc = hal_spi_port_close(&bus->port);
    if (ret_is_err(rc)) return spi_map_port_to_hal(rc);

    /* 有互斥锁就删除掉 */
    if (bus->lock_valid) {
        (void)OSAL_mutex_delete(bus->lock);
        bus->lock_valid = false;
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
    if (!out_dev) return SPI_RC_PARAM(RET_R_NULL_PTR);
    *out_dev = NULL;
    /* 确保总线已经初始化 */
    if (!bus || !bus->initialized) return SPI_RC_STATE(RET_R_NOT_READY);
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
    if (!dev) return SPI_RC_PARAM(RET_R_NULL_PTR);
    if (!dev->in_use) return SPI_RC_STATE(RET_R_NOT_READY);
    dev->evt_cb   = cb;
    dev->evt_user = user;
    return RET_OK;
}
/**
 * @brief 将设备和总线进行解绑
 * @return 32位状态码
 */
ret_code_t hal_spi_dev_detach(hal_spi_dev_t *dev) {
    if (!dev) return SPI_RC_PARAM(RET_R_NULL_PTR);
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
        return spi_map_port_to_hal(rc);
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
        return spi_map_port_to_hal(rc);
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
        return spi_map_port_to_hal(rc);
    }
    /* 没有no_cs 事务就选中cs */
    const bool no_cs = (flags & HAL_SPI_XFER_NO_CS) != 0u;
    if (!no_cs) cs_assert(d);
    /* 调用底层的数据流开始 */
    rc = hal_spi_port_stream_start(&b->port, x);
    if (ret_is_err(rc)) {
        if (!no_cs) cs_deassert(d);
        bus_release_active_xfer(b);
        return spi_map_port_to_hal(rc);
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
    if (ret_is_err(rc)) return spi_map_port_to_hal(rc);
    /* 根据事务 决定是否取消 选中 cs线 */
    const bool no_cs = (b->active_flags & HAL_SPI_XFER_NO_CS) != 0u;
    if (!no_cs) cs_deassert(d);
    bus_release_active_xfer(b);
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
    if (!dev) return SPI_RC_PARAM(RET_R_NULL_PTR);
    if (!dev->in_use || !dev->bus || !dev->bus->initialized) return SPI_RC_STATE(RET_R_NOT_READY);
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
