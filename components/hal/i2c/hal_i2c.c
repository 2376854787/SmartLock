/**
 * @file hal_i2c.c
 * @brief I2C HAL 抽象层（对齐 SPI 的 bus/dev + 异步/同步 + 事件分发模型）
 */
#include "APP_config.h"
#if defined(CFG_FEAT_HAL_I2C) && (CFG_FEAT_HAL_I2C == 1)

#include <limits.h>
#include <string.h>

#include "assert_cus.h"
#include "hal_i2c.h"
#include "hal_i2c_internal.h"
#include "hal_i2c_port.h"
#include "osal.h"
#if defined(CFG_FEAT_LOG_SYSTEM) && (CFG_FEAT_LOG_SYSTEM == 1)
#include "log.h"
#endif

#define I2C_RC_PARAM(reason_)   RET_MAKE_PARAM(RET_MOD_HAL, RET_SUB_HAL_I2C, (reason_))
#define I2C_RC_STATE(reason_)   RET_MAKE_STATE(RET_MOD_HAL, RET_SUB_HAL_I2C, (reason_))
#define I2C_RC_TIMEOUT(reason_) RET_MAKE_TIMEOUT(RET_MOD_HAL, RET_SUB_HAL_I2C, (reason_))
#define I2C_RC_IO(reason_)      RET_MAKE_IO(RET_MOD_HAL, RET_SUB_HAL_I2C, (reason_))
#define I2C_RC_RES(reason_)     RET_MAKE_RESOURCE(RET_MOD_HAL, RET_SUB_HAL_I2C, (reason_))

#ifndef HAL_I2C_DEV_MAX
#define HAL_I2C_DEV_MAX 16u
#endif

/* 总线资源：生命周期 = bus_init ~ bus_deinit */
struct hal_i2c_bus {
    bool initialized;        /* 总线是否已初始化 */
    hal_i2c_bus_cfg_t cfg;   /* 总线静态配置 */
    osal_mutex_t lock;       /* 发起路径互斥锁 */
    bool lock_valid;         /* 锁句柄是否有效（RTOS场景） */
    hal_i2c_port_ctx_t port; /* 底层 port 上下文 */

    volatile uint8_t xfer_busy; /* 当前是否有事务进行中 */
    hal_i2c_dev_t *active_dev;  /* 当前活跃设备 */
    uint32_t active_flags;      /* 当前事务 flags 缓存 */
};

/* 设备资源：生命周期 = dev_attach ~ dev_detach */
struct hal_i2c_dev {
    bool in_use;                      /* 设备槽是否占用 */
    hal_i2c_bus_t *bus;               /* 所属总线 */
    hal_i2c_dev_cfg_t cfg;            /* 设备配置快照 */
    hal_i2c_evt_cb_t evt_cb;          /* 设备事件回调 */
    void *evt_user;                   /* 回调用户上下文 */
    hal_i2c_sync_observer_t sync_obs; /* 同步观察者（供 sync 模块等待） */
    void *sync_obs_user;              /* 同步观察者上下文 */
};

static struct hal_i2c_bus s_buses[HAL_I2C_BUS_MAX];
static struct hal_i2c_dev s_devs[HAL_I2C_DEV_MAX];

/**
 * @brief port 错误映射日志钩子（弱定义）
 * @param rc_port port 层错误码
 * @param rc_hal  映射后的 hal 错误码
 * @param api     触发映射的 API 名
 * @param arg0    调试参数0
 * @param arg1    调试参数1
 */
__attribute__((weak)) void hal_i2c_on_port_error(ret_code_t rc_port, ret_code_t rc_hal,
                                                 const char *api, uint32_t arg0, uint32_t arg1) {
#if defined(CFG_FEAT_LOG_SYSTEM) && (CFG_FEAT_LOG_SYSTEM == 1) && \
    defined(CFG_PARAM_I2C_LOG_PORT_ERR) && (CFG_PARAM_I2C_LOG_PORT_ERR == 1)
#if defined(CFG_PARAM_I2C_LOG_PORT_ERR_IN_ISR) && (CFG_PARAM_I2C_LOG_PORT_ERR_IN_ISR == 1)
    if (ret_is_err(rc_port)) {
#else
    if (ret_is_err(rc_port) && !OSAL_in_isr()) {
#endif
        LOG_E("HAL_I2C", "api:%s port:0x%08lX->hal:0x%08lX cls:%u reason:%u arg0:%lu arg1:%lu",
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

/**
 * @brief 将 port 层错误码映射为 HAL I2C 统一错误码
 * @param rc_port port 返回码
 * @param api     API 名（用于日志钩子）
 * @param arg0    调试参数0
 * @param arg1    调试参数1
 * @return HAL 语义错误码
 */
static inline ret_code_t i2c_map_port_to_hal(ret_code_t rc_port, const char *api, uint32_t arg0,
                                             uint32_t arg1) {
    if (ret_is_ok(rc_port)) return RET_OK;

    ret_code_t rc_hal = I2C_RC_IO(RET_R_IO);

    if (ret_is_class(rc_port, RET_CLASS_PARAM)) {
        if (ret_is_reason(rc_port, RET_R_NULL_PTR))
            rc_hal = I2C_RC_PARAM(RET_R_NULL_PTR);
        else if (ret_is_reason(rc_port, RET_R_RANGE_ERR))
            rc_hal = I2C_RC_PARAM(RET_R_RANGE_ERR);
        else if (ret_is_reason(rc_port, RET_R_UNSUPPORTED))
            rc_hal = I2C_RC_PARAM(RET_R_UNSUPPORTED);
        else
            rc_hal = I2C_RC_PARAM(RET_R_INVALID_ARG);
    } else if (ret_is_class(rc_port, RET_CLASS_TIMEOUT)) {
        rc_hal = I2C_RC_TIMEOUT(RET_R_TIMEOUT);
    } else if (ret_is_class(rc_port, RET_CLASS_RESOURCE)) {
        if (ret_is_reason(rc_port, RET_R_NO_MEM))
            rc_hal = I2C_RC_RES(RET_R_NO_MEM);
        else
            rc_hal = I2C_RC_RES(RET_R_NO_RESOURCE);
    } else if (ret_is_class(rc_port, RET_CLASS_STATE)) {
        if (ret_is_reason(rc_port, RET_R_BUSY))
            rc_hal = I2C_RC_STATE(RET_R_BUSY);
        else if (ret_is_reason(rc_port, RET_R_NOT_READY))
            rc_hal = I2C_RC_STATE(RET_R_NOT_READY);
        else
            rc_hal = I2C_RC_STATE(RET_R_STATE_ERR);
    }

    hal_i2c_on_port_error(rc_port, rc_hal, api, arg0, arg1);
    return rc_hal;
}

static ret_code_t i2c_map_runtime_to_hal(ret_code_t rc_runtime) {
    if (ret_is_ok(rc_runtime)) return RET_OK;

    if (ret_is_class(rc_runtime, RET_CLASS_PARAM)) {
        if (ret_is_reason(rc_runtime, RET_R_NULL_PTR))
            return I2C_RC_PARAM(RET_R_NULL_PTR);
        else if (ret_is_reason(rc_runtime, RET_R_RANGE_ERR))
            return I2C_RC_PARAM(RET_R_RANGE_ERR);
        else
            return I2C_RC_PARAM(RET_R_INVALID_ARG);
    }

    if (ret_is_class(rc_runtime, RET_CLASS_TIMEOUT)) return I2C_RC_TIMEOUT(RET_R_TIMEOUT);

    if (ret_is_class(rc_runtime, RET_CLASS_RESOURCE)) {
        if (ret_is_reason(rc_runtime, RET_R_NO_MEM))
            return I2C_RC_RES(RET_R_NO_MEM);
        else
            return I2C_RC_RES(RET_R_NO_RESOURCE);
    }

    if (ret_is_class(rc_runtime, RET_CLASS_STATE)) {
        if (ret_is_reason(rc_runtime, RET_R_BUSY))
            return I2C_RC_STATE(RET_R_BUSY);
        else if (ret_is_reason(rc_runtime, RET_R_NOT_READY))
            return I2C_RC_STATE(RET_R_NOT_READY);
        else
            return I2C_RC_STATE(RET_R_STATE_ERR);
    }

    return I2C_RC_IO(RET_R_IO);
}

/**
 * @brief 判断指定总线是否仍有设备挂载
 * @param bus 总线句柄
 * @return true: 有设备；false: 无设备
 */
static bool bus_has_attached_dev(const hal_i2c_bus_t *bus) {
    if (!bus) return false;
    osal_crit_state_t cs = 0u;
    bool found           = false;
    OSAL_enter_critical_ex(&cs);
    for (uint32_t i = 0; i < HAL_I2C_DEV_MAX; i++) {
        if (s_devs[i].in_use && (s_devs[i].bus == bus)) {
            found = true;
            break;
        }
    }
    OSAL_exit_critical_ex(cs);
    return found;
}

/**
 * @brief 从设备池分配一个设备槽位
 * @param bus 所属总线
 * @param cfg 设备配置
 * @return 分配成功返回设备指针，失败返回 NULL
 */
static hal_i2c_dev_t *reserve_dev_slot(hal_i2c_bus_t *bus, const hal_i2c_dev_cfg_t *cfg) {
    if (!bus || !cfg) return NULL;
    hal_i2c_dev_t *d     = NULL;
    osal_crit_state_t cs = 0u;
    OSAL_enter_critical_ex(&cs);
    for (uint32_t i = 0; i < HAL_I2C_DEV_MAX; i++) {
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

/**
 * @brief 释放设备槽位
 * @param d 设备指针
 */
static void release_dev_slot(hal_i2c_dev_t *d) {
    if (!d) return;
    osal_crit_state_t cs = 0u;
    OSAL_enter_critical_ex(&cs);
    memset(d, 0, sizeof(*d));
    OSAL_exit_critical_ex(cs);
}

/**
 * @brief 总线互斥锁加锁
 * @param b          总线句柄
 * @param timeout_ms 锁等待超时
 * @return RET_OK 或 OSAL 错误码
 */
static ret_code_t bus_lock(hal_i2c_bus_t *b, uint32_t timeout_ms) {
    REQUIRE_RET(b != NULL, I2C_RC_PARAM(RET_R_NULL_PTR));
    if (!b->lock_valid) return RET_OK;
    return OSAL_mutex_lock(b->lock, timeout_ms);
}

/**
 * @brief 总线互斥锁解锁
 * @param b 总线句柄
 */
static void bus_unlock(hal_i2c_bus_t *b) {
    CORE_ASSERT(b != NULL);
    if (!b || !b->lock_valid) return;
    (void)OSAL_mutex_unlock(b->lock);
}

/**
 * @brief 统一设备事件分发出口
 * @param d   设备句柄
 * @param evt 事件载体
 * @note HAL 只负责同步观察者和设备回调；系统事件分发由上层封装
 */
static inline void emit_dev_evt(const hal_i2c_dev_t *d, const hal_i2c_event_t *evt) {
    if (!d || !evt) return;
    /* 先通知同步观察者，保证同步等待稳定捕获 DONE/ERROR 终态，
     * 再走设备级回调。 */
    if (d->sync_obs) d->sync_obs(d->sync_obs_user, evt);

    if (d->evt_cb) {
#if defined(CFG_PARAM_I2C_CB_IN_ISR) && (CFG_PARAM_I2C_CB_IN_ISR == 1)
        d->evt_cb(d->evt_user, evt);
#else
        if (!OSAL_in_isr()) d->evt_cb(d->evt_user, evt);
#endif
    }
}

/**
 * @brief 检查总线配置合法性
 * @param cfg 总线配置
 * @return RET_OK 或错误码
 */
static ret_code_t cfg_check_bus(const hal_i2c_bus_cfg_t *cfg) {
    /* 非空 */
    REQUIRE_RET(cfg != NULL, I2C_RC_PARAM(RET_R_NULL_PTR));
    /* id 范围检查 */
    REQUIRE_RET(cfg->bus_id < HAL_I2C_BUS_MAX, I2C_RC_PARAM(RET_R_RANGE_ERR));
    /* 默认频率检查 */
    REQUIRE_RET(cfg->default_hz != 0u, I2C_RC_PARAM(RET_R_RANGE_ERR));
    return RET_OK;
}

/**
 * @brief 检查设备配置合法性
 * @param cfg 设备配置
 * @return RET_OK 或错误码
 */
static ret_code_t cfg_check_dev(const hal_i2c_dev_cfg_t *cfg) {
    REQUIRE_RET(cfg != NULL, I2C_RC_PARAM(RET_R_NULL_PTR));
    REQUIRE_RET(cfg->max_hz != 0u, I2C_RC_PARAM(RET_R_RANGE_ERR));
    REQUIRE_RET((cfg->addr_mode == HAL_I2C_ADDR_7BIT) || (cfg->addr_mode == HAL_I2C_ADDR_10BIT),
                I2C_RC_PARAM(RET_R_INVALID_ARG));

    if (cfg->addr_mode == HAL_I2C_ADDR_7BIT) {
        REQUIRE_RET(cfg->dev_addr <= 0x7Fu, I2C_RC_PARAM(RET_R_RANGE_ERR));
    } else {
        REQUIRE_RET(cfg->dev_addr <= 0x3FFu, I2C_RC_PARAM(RET_R_RANGE_ERR));
    }
    return RET_OK;
}

/**
 * @brief I2C API 事务公共参数检查
 * @param dev  设备句柄
 * @param xfer 事务参数
 * @return RET_OK 或错误码
 * @note 当前限制：
 * - 仅支持 master 设备；
 * - tx/rx 不能同发；
 * - NO_STOP 未实现。
 */
static ret_code_t validate_api_xfer_common(const hal_i2c_dev_t *dev, const hal_i2c_xfer_t *xfer) {
    REQUIRE_RET((dev != NULL) && (xfer != NULL), I2C_RC_PARAM(RET_R_NULL_PTR));
    if (!dev->in_use || (dev->bus == NULL) || !dev->bus->initialized)
        return I2C_RC_STATE(RET_R_NOT_READY);
    if (!dev->cfg.is_master) return I2C_RC_PARAM(RET_R_UNSUPPORTED);

    REQUIRE_RET((xfer->tx_len > 0u) || (xfer->rx_len > 0u), I2C_RC_PARAM(RET_R_RANGE_ERR));
    REQUIRE_RET((xfer->tx_len == 0u) || (xfer->tx != NULL), I2C_RC_PARAM(RET_R_NULL_PTR));
    REQUIRE_RET((xfer->rx_len == 0u) || (xfer->rx != NULL), I2C_RC_PARAM(RET_R_NULL_PTR));
    if ((xfer->flags & ~((uint32_t)HAL_I2C_XFER_NO_STOP)) != 0u)
        return I2C_RC_PARAM(RET_R_INVALID_ARG);
    if ((xfer->flags & HAL_I2C_XFER_NO_STOP) != 0u) return I2C_RC_PARAM(RET_R_UNSUPPORTED);

    if ((xfer->tx_len > 0u) && (xfer->rx_len > 0u)) return I2C_RC_PARAM(RET_R_UNSUPPORTED);

    return RET_OK;
}

/**
 * @brief 原子申请当前活跃事务槽
 * @param b     总线句柄
 * @param d     活跃设备
 * @param flags 事务 flags
 * @return RET_OK 或 BUSY
 */
static ret_code_t bus_claim_active_xfer(hal_i2c_bus_t *b, hal_i2c_dev_t *d, uint32_t flags) {
    osal_crit_state_t cs = 0u;
    OSAL_enter_critical_ex(&cs);
    if (b->xfer_busy) {
        OSAL_exit_critical_ex(cs);
        return I2C_RC_STATE(RET_R_BUSY);
    }
    b->xfer_busy    = 1u;
    b->active_dev   = d;
    b->active_flags = flags;
    OSAL_exit_critical_ex(cs);
    return RET_OK;
}

/**
 * @brief 原子释放当前活跃事务槽
 * @param b 总线句柄
 */
static void bus_release_active_xfer(hal_i2c_bus_t *b) {
    if (!b) return;
    osal_crit_state_t cs = 0u;
    OSAL_enter_critical_ex(&cs);
    b->xfer_busy    = 0u;
    b->active_dev   = NULL;
    b->active_flags = 0u;
    OSAL_exit_critical_ex(cs);
}

/**
 * @brief port 层事件回调桥接
 * @param user 总线句柄
 * @param evt  port 事件
 * @note 负责：
 * 1) 释放活跃事务槽；
 * 2) 统一向上分发 HAL 事件。
 */
static void i2c_port_evt_cb(void *user, const hal_i2c_port_evt_t *evt) {
    hal_i2c_bus_t *b = (hal_i2c_bus_t *)user;
    if (!b || !b->initialized || !evt) return;

    hal_i2c_dev_t *dev   = NULL;
    osal_crit_state_t cs = 0u;
    OSAL_enter_critical_ex(&cs);
    dev = b->active_dev;
    OSAL_exit_critical_ex(cs);
    if (!dev) {
        bus_release_active_xfer(b);
        return;
    }

    bus_release_active_xfer(b);

    hal_i2c_event_t hevt = {0};
    if (evt->type == HAL_I2C_PORT_EVT_DONE) {
        hevt.type          = HAL_I2C_EVT_DONE;
        hevt.done.tx_bytes = evt->tx_bytes;
        hevt.done.rx_bytes = evt->rx_bytes;
    } else {
        const ret_code_t rc_hal =
            i2c_map_port_to_hal(evt->rc_port, "i2c_port_evt_cb", evt->tx_bytes, evt->rx_bytes);
        hevt.type   = HAL_I2C_EVT_ERROR;
        hevt.err.rc = rc_hal;
    }
    emit_dev_evt(dev, &hevt);
}

/**
 * @brief 初始化 I2C 总线并初始化 port 资源
 * @details
 * 1. 参数检查与 bus_id 资源占用检查；
 * 2. 初始化底层 port 并注册 port 事件回调；
 * 3. RTOS 场景按需创建 mutex。
 */
ret_code_t hal_i2c_bus_init(const hal_i2c_bus_cfg_t *cfg, hal_i2c_bus_t **out_bus) {
    /* 检查 接收容器 */
    REQUIRE_RET(out_bus != NULL, I2C_RC_PARAM(RET_R_NULL_PTR));
    *out_bus      = NULL;
    /* 检查总线配置 */
    ret_code_t rc = cfg_check_bus(cfg);
    if (ret_is_err(rc)) return rc;
    /* 必须没有被初始化 */
    if (s_buses[cfg->bus_id].initialized) return I2C_RC_RES(RET_R_NO_RESOURCE);
    /* 获取内存池对象 */
    hal_i2c_bus_t *b = &s_buses[cfg->bus_id];
    memset(b, 0, sizeof(*b));
    b->initialized = true;
    b->cfg         = *cfg;
    /* 填充对象 */
    rc             = hal_i2c_port_init(cfg, &b->port);
    if (ret_is_err(rc)) {
        b->initialized = false;
        return i2c_map_port_to_hal(rc, "hal_i2c_port_init", cfg->bus_id, cfg->default_hz);
    }

    rc = hal_i2c_port_set_evt_cb(&b->port, i2c_port_evt_cb, b);
    if (ret_is_err(rc)) {
        (void)hal_i2c_port_deinit(&b->port);
        b->initialized = false;
        return i2c_map_port_to_hal(rc, "hal_i2c_port_set_evt_cb", cfg->bus_id, 0u);
    }

    if (OSAL_kernel_is_running()) {
        if (ret_is_ok(OSAL_mutex_create(&b->lock, "i2c_bus", false, true))) {
            b->lock_valid = true;
        }
    }
    *out_bus = b;
    return RET_OK;
}

/**
 * @brief 反初始化 I2C 总线并释放互斥资源
 * @note 反初始化前必须满足：
 * - 当前总线无活跃事务；
 * - 无设备仍挂载在该总线上。
 */
ret_code_t hal_i2c_bus_deinit(hal_i2c_bus_t *bus) {
    REQUIRE_RET(bus != NULL, I2C_RC_PARAM(RET_R_NULL_PTR));
    if (!bus->initialized) return I2C_RC_STATE(RET_R_NOT_READY);
    if (bus->xfer_busy) return I2C_RC_STATE(RET_R_BUSY);
    if (bus_has_attached_dev(bus)) return I2C_RC_STATE(RET_R_BUSY);

    const ret_code_t rc = hal_i2c_port_deinit(&bus->port);
    if (ret_is_err(rc)) return i2c_map_port_to_hal(rc, "hal_i2c_port_deinit", bus->cfg.bus_id, 0u);

    if (bus->lock_valid) {
        (void)OSAL_mutex_delete(bus->lock);
        bus->lock_valid = false;
    }
    bus->initialized = false;
    return RET_OK;
}

/**
 * @brief 挂载 I2C 设备到总线
 * @note 仅占用 HAL 设备槽并缓存配置，硬件应用延后到发起事务前
 */
ret_code_t hal_i2c_dev_attach(hal_i2c_bus_t *bus, const hal_i2c_dev_cfg_t *cfg,
                              hal_i2c_dev_t **out_dev) {
    REQUIRE_RET(out_dev != NULL, I2C_RC_PARAM(RET_R_NULL_PTR));
    *out_dev = NULL;
    REQUIRE_RET(bus != NULL, I2C_RC_PARAM(RET_R_NULL_PTR));
    if (!bus->initialized) return I2C_RC_STATE(RET_R_NOT_READY);

    ret_code_t rc = cfg_check_dev(cfg);
    if (ret_is_err(rc)) return rc;

    hal_i2c_dev_t *d = reserve_dev_slot(bus, cfg);
    if (!d) return I2C_RC_RES(RET_R_NO_RESOURCE);

    *out_dev = d;
    return RET_OK;
}

/**
 * @brief 解绑 I2C 设备
 * @note 若该设备是当前活跃事务设备，将返回 BUSY
 */
ret_code_t hal_i2c_dev_detach(hal_i2c_dev_t *dev) {
    REQUIRE_RET(dev != NULL, I2C_RC_PARAM(RET_R_NULL_PTR));
    if (!dev->in_use) return I2C_RC_STATE(RET_R_NOT_READY);

    if (dev->bus) {
        bool busy_active     = false;
        osal_crit_state_t cs = 0u;
        OSAL_enter_critical_ex(&cs);
        busy_active = (dev->bus->xfer_busy != 0u) && (dev->bus->active_dev == dev);
        OSAL_exit_critical_ex(cs);
        if (busy_active) return I2C_RC_STATE(RET_R_BUSY);
    }
    release_dev_slot(dev);
    return RET_OK;
}

/**
 * @brief 注册 I2C 事件回调
 * @note cb 可为 NULL，用于动态关闭回调路径
 */
ret_code_t hal_i2c_dev_set_evt_cb(hal_i2c_dev_t *dev, hal_i2c_evt_cb_t cb, void *user) {
    REQUIRE_RET(dev != NULL, I2C_RC_PARAM(RET_R_NULL_PTR));
    if (!dev->in_use) return I2C_RC_STATE(RET_R_NOT_READY);
    dev->evt_cb   = cb;
    dev->evt_user = user;
    return RET_OK;
}

/**
 * @brief 注册/注销同步观察者（供同步封装模块使用）
 * @param dev 设备句柄
 * @param cb  观察者回调，传 NULL 表示注销
 * @param user 观察者上下文
 * @return RET_OK 或错误码
 */
ret_code_t hal_i2c_dev_set_sync_observer(hal_i2c_dev_t *dev, hal_i2c_sync_observer_t cb,
                                         void *user) {
    REQUIRE_RET(dev != NULL, I2C_RC_PARAM(RET_R_NULL_PTR));
    if (!dev->in_use) return I2C_RC_STATE(RET_R_NOT_READY);

    /* 在临界区内切换观察者指针，避免与 ISR 事件分发并发竞争。 */
    osal_crit_state_t cs = 0u;
    OSAL_enter_critical_ex(&cs);
    dev->sync_obs      = cb;
    dev->sync_obs_user = user;
    OSAL_exit_critical_ex(cs);
    return RET_OK;
}

/**
 * @brief 在持锁状态下发起一次异步事务
 * @param d 设备句柄
 * @param x 事务参数
 * @return RET_OK 或错误码
 * @details
 * 1. 原子申请 active 事务槽；
 * 2. 应用设备配置到 port；
 * 3. 调用 port 发起异步事务；
 * 4. 任一步失败则回滚 active 槽位。
 */
static ret_code_t i2c_xfer_guarded(hal_i2c_dev_t *d, const hal_i2c_xfer_t *x) {
    hal_i2c_bus_t *b = d->bus;
    ret_code_t rc    = bus_claim_active_xfer(b, d, x->flags);
    if (ret_is_err(rc)) return rc;

    rc = hal_i2c_port_apply(&b->port, &d->cfg, b->cfg.default_hz);
    if (ret_is_err(rc)) {
        bus_release_active_xfer(b);
        return i2c_map_port_to_hal(rc, "hal_i2c_port_apply", b->cfg.bus_id, 0u);
    }

    rc = hal_i2c_port_xfer(&b->port, &d->cfg, x);
    if (ret_is_err(rc)) {
        bus_release_active_xfer(b);
        return i2c_map_port_to_hal(rc, "hal_i2c_port_xfer", b->cfg.bus_id, x->tx_len + x->rx_len);
    }
    return RET_OK;
}

/**
 * @brief 发起一次异步 I2C 事务
 * @note 返回 RET_OK 仅代表“发起成功”，不代表“传输成功”
 */
ret_code_t hal_i2c_transceive(hal_i2c_dev_t *dev, const hal_i2c_xfer_t *xfer) {
    ret_code_t rc = validate_api_xfer_common(dev, xfer);
    if (ret_is_err(rc)) return rc;

    hal_i2c_bus_t *b = dev->bus;
    rc               = bus_lock(b, xfer->timeout_ms ? xfer->timeout_ms : OSAL_WAIT_FOREVER);
    if (ret_is_err(rc)) return i2c_map_runtime_to_hal(rc);
    rc = i2c_xfer_guarded(dev, xfer);
    bus_unlock(b);
    return rc;
}

/**
 * @brief 中止当前设备事务（持锁路径）
 * @details
 * 1. 校验当前设备是否为活跃事务设备；
 * 2. 调用 port abort；
 * 3. 释放 active 槽；
 * 4. 向上层统一上报 ABORTED 错误。
 */
static ret_code_t i2c_abort_guarded(hal_i2c_dev_t *d, bool disable_i2c) {
    hal_i2c_bus_t *b = d->bus;
    if (!b || !b->xfer_busy) return I2C_RC_STATE(RET_R_NOT_READY);
    if (b->active_dev != d) return I2C_RC_STATE(RET_R_BUSY);

    const ret_code_t rc = hal_i2c_port_abort(&b->port, disable_i2c);
    if (ret_is_err(rc))
        return i2c_map_port_to_hal(rc, "hal_i2c_port_abort", b->cfg.bus_id, disable_i2c ? 1u : 0u);

    bus_release_active_xfer(b);

    const ret_code_t abort_rc = I2C_RC_STATE(RET_R_ABORTED);
    hal_i2c_event_t evt       = {0};
    evt.type                  = HAL_I2C_EVT_ERROR;
    evt.err.rc                = abort_rc;
    emit_dev_evt(d, &evt);

    return RET_OK;
}

/**
 * @brief 对外中止接口
 * @note 该接口会先获取总线互斥锁，再进入 guarded 中止路径
 */
ret_code_t hal_i2c_abort(hal_i2c_dev_t *dev, bool disable_i2c) {
    REQUIRE_RET(dev != NULL, I2C_RC_PARAM(RET_R_NULL_PTR));
    if (!dev->in_use || (dev->bus == NULL) || !dev->bus->initialized)
        return I2C_RC_STATE(RET_R_NOT_READY);

    hal_i2c_bus_t *b = dev->bus;
    ret_code_t rc    = bus_lock(b, OSAL_WAIT_FOREVER);
    if (ret_is_err(rc)) return i2c_map_runtime_to_hal(rc);
    rc = i2c_abort_guarded(dev, disable_i2c);
    bus_unlock(b);
    return rc;
}

#endif
