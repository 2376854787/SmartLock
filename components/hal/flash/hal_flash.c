#include "hal_flash.h"

#include "APP_config.h"

#define FLASH_HAL_PARAM(reason_)   RET_MAKE_PARAM(RET_MOD_HAL, RET_SUB_HAL_FLASH, (reason_))
#define FLASH_HAL_STATE(reason_)   RET_MAKE_STATE(RET_MOD_HAL, RET_SUB_HAL_FLASH, (reason_))
#define FLASH_HAL_TIMEOUT(reason_) RET_MAKE_TIMEOUT(RET_MOD_HAL, RET_SUB_HAL_FLASH, (reason_))
#define FLASH_HAL_IO(reason_)      RET_MAKE_IO(RET_MOD_HAL, RET_SUB_HAL_FLASH, (reason_))
#define FLASH_HAL_RES(reason_)     RET_MAKE_RESOURCE(RET_MOD_HAL, RET_SUB_HAL_FLASH, (reason_))
#define FLASH_HAL_DATA(reason_)    RET_MAKE_DATA(RET_MOD_HAL, RET_SUB_HAL_FLASH, (reason_))

#if defined(CFG_FEAT_HAL_FLASH) && (CFG_FEAT_HAL_FLASH == 1)

#include <string.h>

#include "assert_cus.h"
#include "hal_flash_port.h"
#include "osal.h"
#if defined(CFG_FEAT_LOG_SYSTEM) && (CFG_FEAT_LOG_SYSTEM == 1)
#include "log.h"
#endif

#ifndef CFG_PARAM_FLASH_WORKER_STACK_SIZE
#define CFG_PARAM_FLASH_WORKER_STACK_SIZE 1024u /* RTOS 任务栈大小 */
#endif

#define FLASH_WORKER_FLAG_RUN    (1u << 0)
#define FLASH_COMPARE_SLOW_CHUNK 32u
#define FLASH_COMPARE_FAST_CHUNK 128u

/* HAL 只维护一个在途作业，保持与底层 Flash 独占访问一致。 */
typedef enum {
    HAL_FLASH_JOB_TYPE_NONE = 0,    /* 无待处理作业 */
    HAL_FLASH_JOB_TYPE_READ,        /* 读 Flash 到 RAM */
    HAL_FLASH_JOB_TYPE_ERASE,       /* 擦除指定地址范围 */
    HAL_FLASH_JOB_TYPE_WRITE,       /* 将 RAM 数据编程到 Flash */
    HAL_FLASH_JOB_TYPE_COMPARE,     /* 读取 Flash 并与期望数据比较 */
    HAL_FLASH_JOB_TYPE_BLANK_CHECK, /* 检查目标区域是否全为擦除值 */
} hal_flash_job_type_t;

typedef struct {
    hal_flash_job_type_t type; /* 作业类型 */
    uint32_t addr;             /* Flash 绝对起始地址 */
    uint32_t len;              /* 本次作业作用长度，单位字节 */
    void *dst;                 /* READ 的输出缓冲区，其它作业为 NULL */
    const void *src;           /* WRITE/COMPARE 的输入缓冲区，其它作业为 NULL */
    bool *blank_out;           /* BLANK_CHECK 的输出结果地址，其它作业为 NULL */
} hal_flash_job_t;

typedef struct {
    bool initialized;                  /* 模块是否已完成 init */
    bool dispatching;                  /* 当前 job 是否已被 MainFunction/worker 接管执行 */
    bool worker_enabled;               /* 是否启用后台 worker 模式 */
    bool worker_created;               /* 后台 worker 是否已成功创建 */
    volatile uint8_t cancel_requested; /* 取消请求标记；执行中仅做 best-effort 收敛 */
    osal_mutex_t lock;                 /* 保护控制块和 job 状态的互斥锁 */
    bool lock_valid;                   /* lock 是否可用；裸机场景可能为 false */
    osal_thread_t worker;              /* 后台执行异步 job 的 worker 线程句柄 */
    hal_flash_status_t status;         /* 模块状态：UNINIT/IDLE/BUSY */
    hal_flash_job_result_t job_result; /* 当前或最近一次作业结果 */
    hal_flash_mode_t mode;             /* 当前作业模式 */
    hal_flash_cfg_t cfg;               /* 用户配置与通知回调 */
    hal_flash_job_t job;               /* 当前唯一在途或待执行的作业描述 */
} hal_flash_ctrl_t;
/* 全局控制 */
static hal_flash_ctrl_t s_flash;

CORE_WEAK void hal_flash_on_port_error(ret_code_t rc_port, ret_code_t rc_hal, const char *api,
                                       uint32_t arg0, uint32_t arg1) {
#if defined(CFG_FEAT_LOG_SYSTEM) && (CFG_FEAT_LOG_SYSTEM == 1) && \
    defined(CFG_PARAM_FLASH_LOG_PORT_ERR) && (CFG_PARAM_FLASH_LOG_PORT_ERR == 1)
#if defined(CFG_PARAM_FLASH_LOG_PORT_ERR_IN_ISR) && (CFG_PARAM_FLASH_LOG_PORT_ERR_IN_ISR == 1)
    if (ret_is_err(rc_port)) {
#else
    if (ret_is_err(rc_port) && !OSAL_in_isr()) {
#endif
        LOG_E("HAL_FLASH", "api:%s port:0x%08lX->hal:0x%08lX arg0:%lu arg1:%lu",
              (api != NULL) ? api : "unknown", (unsigned long)rc_port, (unsigned long)rc_hal,
              (unsigned long)arg0, (unsigned long)arg1);
    }
#else
    (void)rc_port;
    (void)rc_hal;
    (void)api;
    (void)arg0;
    (void)arg1;
#endif
}

CORE_INLINE ret_code_t flash_map_port_to_hal(ret_code_t rc_port, const char *api, uint32_t arg0,
                                             uint32_t arg1) {
    if (ret_is_ok(rc_port)) return RET_OK;

    ret_code_t rc_hal = FLASH_HAL_IO(RET_R_FLASH_ERR);

    if (ret_is_class(rc_port, RET_CLASS_PARAM)) {
        if (ret_is_reason(rc_port, RET_R_NULL_PTR))
            rc_hal = FLASH_HAL_PARAM(RET_R_NULL_PTR);
        else if (ret_is_reason(rc_port, RET_R_RANGE_ERR))
            rc_hal = FLASH_HAL_PARAM(RET_R_RANGE_ERR);
        else if (ret_is_reason(rc_port, RET_R_UNSUPPORTED))
            rc_hal = FLASH_HAL_PARAM(RET_R_UNSUPPORTED);
        else
            rc_hal = FLASH_HAL_PARAM(RET_R_INVALID_ARG);
    } else if (ret_is_class(rc_port, RET_CLASS_TIMEOUT)) {
        rc_hal = FLASH_HAL_TIMEOUT(RET_R_TIMEOUT);
    } else if (ret_is_class(rc_port, RET_CLASS_RESOURCE)) {
        if (ret_is_reason(rc_port, RET_R_NO_MEM))
            rc_hal = FLASH_HAL_RES(RET_R_NO_MEM);
        else
            rc_hal = FLASH_HAL_RES(RET_R_NO_RESOURCE);
    } else if (ret_is_class(rc_port, RET_CLASS_STATE)) {
        if (ret_is_reason(rc_port, RET_R_BUSY))
            rc_hal = FLASH_HAL_STATE(RET_R_BUSY);
        else if (ret_is_reason(rc_port, RET_R_NOT_READY))
            rc_hal = FLASH_HAL_STATE(RET_R_NOT_READY);
        else if (ret_is_reason(rc_port, RET_R_ABORTED))
            rc_hal = FLASH_HAL_STATE(RET_R_ABORTED);
        else
            rc_hal = FLASH_HAL_STATE(RET_R_STATE_ERR);
    } else if (ret_is_class(rc_port, RET_CLASS_IO)) {
        rc_hal = FLASH_HAL_IO(RET_R_FLASH_ERR);
    }

    hal_flash_on_port_error(rc_port, rc_hal, api, arg0, arg1);
    return rc_hal;
}
static void flash_clear_job_locked(void) {
    s_flash.job.type      = HAL_FLASH_JOB_TYPE_NONE;
    s_flash.job.addr      = 0u;
    s_flash.job.len       = 0u;
    s_flash.job.dst       = NULL;
    s_flash.job.src       = NULL;
    s_flash.job.blank_out = NULL;
}
static ret_code_t flash_lock(uint32_t timeout_ms) {
    if (!s_flash.lock_valid) return RET_OK;
    return OSAL_mutex_lock(s_flash.lock, timeout_ms);
}

static void flash_unlock(void) {
    if (!s_flash.lock_valid) return;
    (void)OSAL_mutex_unlock(s_flash.lock);
}

static ret_code_t flash_read_direct(uint32_t addr, void *dst, uint32_t len) {
    const ret_code_t rc = hal_flash_port_read(addr, dst, len);
    if (ret_is_err(rc)) return flash_map_port_to_hal(rc, "hal_flash_port_read", addr, len);
    return RET_OK;
}

static ret_code_t flash_erase_direct(uint32_t addr, uint32_t len) {
    const ret_code_t rc = hal_flash_port_erase(addr, len);
    if (ret_is_err(rc)) return flash_map_port_to_hal(rc, "hal_flash_port_erase", addr, len);
    return RET_OK;
}

static ret_code_t flash_write_direct(uint32_t addr, const void *src, uint32_t len) {
    const ret_code_t rc = hal_flash_port_write(addr, src, len);
    if (ret_is_err(rc)) return flash_map_port_to_hal(rc, "hal_flash_port_write", addr, len);
    return RET_OK;
}

static ret_code_t flash_blank_check_direct(uint32_t addr, uint32_t len, bool *out) {
    const ret_code_t rc = hal_flash_port_blank_check(addr, len, out);
    if (ret_is_err(rc)) return flash_map_port_to_hal(rc, "hal_flash_port_blank_check", addr, len);
    return RET_OK;
}

static void flash_port_evt_handler(void *user, hal_flash_port_evt_t evt) {
    hal_flash_job_notify_t cb = NULL;
    void *cb_user             = NULL;
    osal_crit_state_t cs      = 0u;
    const hal_flash_job_result_t result =
        (evt == HAL_FLASH_PORT_EVT_DONE) ? HAL_FLASH_JOB_OK : HAL_FLASH_JOB_FAILED;

    (void)user;

    if (OSAL_in_isr())
        OSAL_enter_critical_from_isr(&cs);
    else
        OSAL_enter_critical_ex(&cs);

    if (s_flash.initialized && s_flash.dispatching &&
        (s_flash.job_result == HAL_FLASH_JOB_PENDING) &&
        ((s_flash.job.type == HAL_FLASH_JOB_TYPE_ERASE) ||
         (s_flash.job.type == HAL_FLASH_JOB_TYPE_WRITE))) {
        s_flash.status     = HAL_FLASH_STATUS_IDLE;
        s_flash.job_result = result;
        flash_clear_job_locked();
        s_flash.dispatching      = false;
        s_flash.cancel_requested = 0u;

        if ((result == HAL_FLASH_JOB_OK) && (s_flash.cfg.job_end_notify != NULL))
            cb = s_flash.cfg.job_end_notify;
        else if ((result != HAL_FLASH_JOB_OK) && (s_flash.cfg.job_error_notify != NULL))
            cb = s_flash.cfg.job_error_notify;
        cb_user = s_flash.cfg.user;
    }

    if (OSAL_in_isr())
        OSAL_exit_critical_from_isr(cs);
    else
        OSAL_exit_critical_ex(cs);

    if (cb != NULL) cb(cb_user, result);
}

static uint32_t flash_compare_chunk_size(void) {
    return (s_flash.mode == HAL_FLASH_MODE_FAST) ? FLASH_COMPARE_FAST_CHUNK
                                                 : FLASH_COMPARE_SLOW_CHUNK;
}

static ret_code_t flash_compare_direct(uint32_t addr, const void *src, uint32_t len) {
    uint8_t buf[FLASH_COMPARE_FAST_CHUNK];
    const uint8_t *expect = (const uint8_t *)src;
    uint32_t offset       = 0u;

    /* Compare 复用 read port，按小块比对，避免大栈和长临界路径。 */
    while (offset < len) {
        const uint32_t remaining = len - offset;
        const uint32_t chunk =
            (remaining < flash_compare_chunk_size()) ? remaining : flash_compare_chunk_size();
        const ret_code_t rc = hal_flash_port_read(addr + offset, buf, chunk);
        if (ret_is_err(rc))
            return flash_map_port_to_hal(rc, "hal_flash_port_read", addr + offset, chunk);
        if (memcmp(buf, expect + offset, chunk) != 0) return FLASH_HAL_DATA(RET_R_DATA_MISMATCH);
        offset += chunk;
    }
    return RET_OK;
}

static void flash_emit_notify(hal_flash_job_result_t result) {
    hal_flash_job_notify_t cb = NULL;
    void *user                = NULL;

    /* 回调在锁外执行，避免用户代码反入模块时造成死锁。 */
    if (flash_lock(OSAL_WAIT_FOREVER) == RET_OK) {
        if ((result == HAL_FLASH_JOB_OK) && (s_flash.cfg.job_end_notify != NULL))
            cb = s_flash.cfg.job_end_notify;
        else if ((result != HAL_FLASH_JOB_OK) && (s_flash.cfg.job_error_notify != NULL))
            cb = s_flash.cfg.job_error_notify;
        user = s_flash.cfg.user;
        flash_unlock();
    }

    if (cb != NULL) cb(user, result);
}

static bool flash_try_take_job(hal_flash_job_t *out) {
    bool ready = false;

    if (flash_lock(OSAL_WAIT_FOREVER) != RET_OK) return false;
    /* 已经初始化 & 必须是有任务且未完成任务 &  任务没有被 接管*/
    if (s_flash.initialized && (s_flash.status == HAL_FLASH_STATUS_BUSY) &&
        (s_flash.job_result == HAL_FLASH_JOB_PENDING) && !s_flash.dispatching &&
        (s_flash.job.type != HAL_FLASH_JOB_TYPE_NONE)) {
        s_flash.dispatching = true;        /* 标记已接管 */
        *out                = s_flash.job; /* 输出工作内容 */
        ready               = true;        /* 告诉调用方 有任务需要被接管处理 */
    }
    flash_unlock();
    return ready;
}

static void flash_finish_job(hal_flash_job_result_t result) {
    /* 锁  更改全局状态参数*/
    if (flash_lock(OSAL_WAIT_FOREVER) == RET_OK) {
        s_flash.status     = HAL_FLASH_STATUS_IDLE;
        s_flash.job_result = result;
        flash_clear_job_locked();
        s_flash.dispatching      = false;
        s_flash.cancel_requested = 0u;
        flash_unlock();
    }

    flash_emit_notify(result);
}

static void flash_worker_entry(void *arg) {
    (void)arg;
    for (;;) {
        (void)OSAL_thread_flags_wait(FLASH_WORKER_FLAG_RUN, OSAL_FLAGS_WAIT_ANY, OSAL_WAIT_FOREVER);
        hal_flash_main_function();
    }
}
/**
 * @brief 初始化全局配置
 * @param cfg 配置
 * @return 状态码
 */
ret_code_t hal_flash_init(const hal_flash_cfg_t *cfg) {
    if (s_flash.initialized) return FLASH_HAL_STATE(RET_R_BUSY);
    /* 初始化 默认状态*/
    memset(&s_flash, 0, sizeof(s_flash));
    s_flash.status     = HAL_FLASH_STATUS_IDLE;
    s_flash.job_result = HAL_FLASH_JOB_NONE;
    s_flash.mode       = HAL_FLASH_MODE_FAST;
    /*　NULL 设置默认状态　*/
    if (cfg != NULL) {
        s_flash.cfg = *cfg;
    } else {
        s_flash.cfg.enable_background_worker = true;
        s_flash.cfg.worker_stack_size        = 0u;
    }

    const ret_code_t rc = hal_flash_port_set_evt_cb(flash_port_evt_handler, NULL);
    if (ret_is_err(rc)) return flash_map_port_to_hal(rc, "hal_flash_port_set_evt_cb", 0u, 0u);
    /* RTOS 环境创建互斥锁 */
    if (OSAL_kernel_is_running()) {
        const ret_code_t rc_lock = OSAL_mutex_create(&s_flash.lock, "hal_flash", false, true);
        if (ret_is_err(rc_lock)) return FLASH_HAL_RES(RET_R_NO_RESOURCE);
        s_flash.lock_valid = true;
    }
    /* 使能RTOS 任务 & RTOS 内核运行  -->  创建任务*/
    if (s_flash.cfg.enable_background_worker && OSAL_kernel_is_running()) {
        const osal_thread_attr_t attr = {
            .name       = "flash",
            .stack_size = (s_flash.cfg.worker_stack_size != 0u) ? s_flash.cfg.worker_stack_size
                                                                : CFG_PARAM_FLASH_WORKER_STACK_SIZE,
            .priority   = OSAL_PRIO_NORMAL,
        };
        const ret_code_t rc_thread =
            OSAL_thread_create(&s_flash.worker, flash_worker_entry, NULL, &attr);
        /* 处理失败的场景 */
        if (ret_is_err(rc_thread)) {
            if (s_flash.lock_valid) {
                (void)OSAL_mutex_delete(s_flash.lock);
                s_flash.lock       = NULL;
                s_flash.lock_valid = false;
            }
            return FLASH_HAL_RES(RET_R_NO_RESOURCE);
        }
        s_flash.worker_created = true;
        s_flash.worker_enabled = true;
    }

    s_flash.initialized = true;
    return RET_OK;
}
/**============================================================================================ */
/**==================================      GET/SET     ===================================== */
/**============================================================================================ */
ret_code_t hal_flash_get_status(hal_flash_status_t *out) {
    REQUIRE_RET(out != NULL, FLASH_HAL_PARAM(RET_R_NULL_PTR));
    *out = s_flash.initialized ? s_flash.status : HAL_FLASH_STATUS_UNINIT;
    return RET_OK;
}

ret_code_t hal_flash_get_job_result(hal_flash_job_result_t *out) {
    REQUIRE_RET(out != NULL, FLASH_HAL_PARAM(RET_R_NULL_PTR));
    *out = s_flash.initialized ? s_flash.job_result : HAL_FLASH_JOB_NONE;
    return RET_OK;
}

ret_code_t hal_flash_set_mode(hal_flash_mode_t mode) {
    REQUIRE_RET(s_flash.initialized, FLASH_HAL_STATE(RET_R_NOT_READY));
    REQUIRE_RET((mode == HAL_FLASH_MODE_SLOW) || (mode == HAL_FLASH_MODE_FAST),
                FLASH_HAL_PARAM(RET_R_INVALID_ARG));
    /* 获取锁 */
    if (flash_lock(OSAL_WAIT_FOREVER) != RET_OK) return FLASH_HAL_STATE(RET_R_BUSY);
    /* 只能 flash 在空闲的时候进行 模式设置 */
    if (s_flash.status != HAL_FLASH_STATUS_IDLE) {
        flash_unlock();
        return FLASH_HAL_STATE(RET_R_BUSY);
    }
    s_flash.mode = mode;
    flash_unlock();
    return RET_OK;
}
ret_code_t hal_flash_get_info(hal_flash_info_t *out) {
    REQUIRE_RET(out != NULL, FLASH_HAL_PARAM(RET_R_NULL_PTR));
    const ret_code_t rc = hal_flash_port_get_info(out);
    if (ret_is_err(rc)) return flash_map_port_to_hal(rc, "hal_flash_get_info", 0u, 0u);
    return RET_OK;
}

ret_code_t hal_flash_get_region(uint32_t addr, hal_flash_region_t *out) {
    REQUIRE_RET(out != NULL, FLASH_HAL_PARAM(RET_R_NULL_PTR));
    const ret_code_t rc = hal_flash_port_get_region(addr, out);
    if (ret_is_err(rc)) return flash_map_port_to_hal(rc, "hal_flash_get_region", addr, 0u);
    return RET_OK;
}

ret_code_t hal_flash_cancel(void) {
    REQUIRE_RET(s_flash.initialized, FLASH_HAL_STATE(RET_R_NOT_READY));
    REQUIRE_RET(!OSAL_in_isr(), FLASH_HAL_STATE(RET_R_STATE_ERR));
    /* 获取锁 */
    if (flash_lock(OSAL_WAIT_FOREVER) != RET_OK) return FLASH_HAL_STATE(RET_R_BUSY);
    /* flash 非忙状态 | 工作pending | NONE type 不能取消  */
    if ((s_flash.status != HAL_FLASH_STATUS_BUSY) ||
        (s_flash.job_result != HAL_FLASH_JOB_PENDING) ||
        (s_flash.job.type == HAL_FLASH_JOB_TYPE_NONE)) {
        flash_unlock();
        return FLASH_HAL_STATE(RET_R_NOT_READY);
    }
    if (s_flash.dispatching && ((s_flash.job.type == HAL_FLASH_JOB_TYPE_ERASE) ||
                                (s_flash.job.type == HAL_FLASH_JOB_TYPE_WRITE))) {
        flash_unlock();
        return FLASH_HAL_STATE(RET_R_BUSY);
    }
    /* 裸机下的主任务 手动处理  */
    if (!s_flash.dispatching) {
        s_flash.status     = HAL_FLASH_STATUS_IDLE;
        s_flash.job_result = HAL_FLASH_JOB_CANCELED;
        flash_clear_job_locked();
        s_flash.dispatching      = false;
        s_flash.cancel_requested = 0u;
        flash_unlock();
        flash_emit_notify(HAL_FLASH_JOB_CANCELED);
        return RET_OK;
    }
    /* 标记取消请求 */
    s_flash.cancel_requested = 1u;
    flash_unlock();
    return RET_OK;
}
/**
 * @brief 提交异步 API 提供的工作事务 等待work 函数进行调用
 * @param type 工作类型
 * @param addr 基地址
 * @param len 长度
 * @param dst 数据接收地址
 * @param src 数据源地址
 * @param blank_out  接收是否为空
 * @return  状态码
 */
static ret_code_t flash_schedule_job(hal_flash_job_type_t type, uint32_t addr, uint32_t len,
                                     void *dst, const void *src, bool *blank_out) {
    REQUIRE_RET(!OSAL_in_isr(), FLASH_HAL_STATE(RET_R_STATE_ERR));
    REQUIRE_RET(s_flash.initialized, FLASH_HAL_STATE(RET_R_NOT_READY));

    if (flash_lock(OSAL_WAIT_FOREVER) != RET_OK) return FLASH_HAL_STATE(RET_R_BUSY);
    /* HAL 只允许一个 job 在途，避免 Flash 擦写与读比较并发。 */
    if (s_flash.status != HAL_FLASH_STATUS_IDLE) {
        flash_unlock();
        return FLASH_HAL_STATE(RET_R_BUSY);
    }
    /* 提交事务 */
    s_flash.job.type           = type;
    s_flash.job.addr           = addr;
    s_flash.job.len            = len;
    s_flash.job.dst            = dst;
    s_flash.job.src            = src;
    s_flash.job.blank_out      = blank_out;
    s_flash.job_result         = HAL_FLASH_JOB_PENDING;
    s_flash.status             = HAL_FLASH_STATUS_BUSY;
    s_flash.dispatching        = false;
    s_flash.cancel_requested   = 0u;

    /* 检查 work函数是否被创建 和 使能 */
    const bool signal_worker   = s_flash.worker_enabled && s_flash.worker_created;
    const osal_thread_t worker = s_flash.worker;
    flash_unlock();

    /* 任务通知唤醒 */
    if (signal_worker) (void)OSAL_thread_flags_set(worker, FLASH_WORKER_FLAG_RUN);
    return RET_OK;
}

ret_code_t hal_flash_read(uint32_t addr, void *dst, uint32_t len) {
    REQUIRE_RET((dst != NULL) && (len != 0u), FLASH_HAL_PARAM(RET_R_INVALID_ARG));
    return flash_schedule_job(HAL_FLASH_JOB_TYPE_READ, addr, len, dst, NULL, NULL);
}

ret_code_t hal_flash_erase(uint32_t addr, uint32_t len) {
    REQUIRE_RET(len != 0u, FLASH_HAL_PARAM(RET_R_RANGE_ERR));
    return flash_schedule_job(HAL_FLASH_JOB_TYPE_ERASE, addr, len, NULL, NULL, NULL);
}

ret_code_t hal_flash_write(uint32_t addr, const void *src, uint32_t len) {
    REQUIRE_RET((src != NULL) && (len != 0u), FLASH_HAL_PARAM(RET_R_INVALID_ARG));
    return flash_schedule_job(HAL_FLASH_JOB_TYPE_WRITE, addr, len, NULL, src, NULL);
}

ret_code_t hal_flash_compare(uint32_t addr, const void *src, uint32_t len) {
    REQUIRE_RET((src != NULL) && (len != 0u), FLASH_HAL_PARAM(RET_R_INVALID_ARG));
    return flash_schedule_job(HAL_FLASH_JOB_TYPE_COMPARE, addr, len, NULL, src, NULL);
}

ret_code_t hal_flash_blank_check(uint32_t addr, uint32_t len, bool *out) {
    REQUIRE_RET((out != NULL) && (len != 0u), FLASH_HAL_PARAM(RET_R_INVALID_ARG));
    return flash_schedule_job(HAL_FLASH_JOB_TYPE_BLANK_CHECK, addr, len, NULL, NULL, out);
}
/**
 * @brief 检查是否有任务需要被处理 根据工作内容选择不同的处理函数进行处理
 */
void hal_flash_main_function(void) {
    /* 初始化检查 */
    if (!s_flash.initialized || OSAL_in_isr()) return;

    hal_flash_job_t job = {0};
    if (!flash_try_take_job(&job)) return;

    /* cancel 仅在 job 尚未结束前做 best-effort 收敛。 */
    if (s_flash.cancel_requested != 0u) {
        flash_finish_job(HAL_FLASH_JOB_CANCELED);
        return;
    }

    ret_code_t rc = FLASH_HAL_STATE(RET_R_STATE_ERR);
    /* 根据设备的工作 类型调用对应的 处理函数 */
    switch (job.type) {
        case HAL_FLASH_JOB_TYPE_READ:
            rc = flash_read_direct(job.addr, job.dst, job.len);
            break;
        case HAL_FLASH_JOB_TYPE_ERASE:
            rc = hal_flash_port_erase_it(job.addr, job.len);
            if (ret_is_ok(rc)) return;
            rc = flash_map_port_to_hal(rc, "hal_flash_port_erase_it", job.addr, job.len);
            break;
        case HAL_FLASH_JOB_TYPE_WRITE:
            rc = hal_flash_port_write_it(job.addr, job.src, job.len);
            if (ret_is_ok(rc)) return;
            rc = flash_map_port_to_hal(rc, "hal_flash_port_write_it", job.addr, job.len);
            break;
        case HAL_FLASH_JOB_TYPE_COMPARE:
            rc = flash_compare_direct(job.addr, job.src, job.len);
            break;
        case HAL_FLASH_JOB_TYPE_BLANK_CHECK:
            rc = flash_blank_check_direct(job.addr, job.len, job.blank_out);
            break;
        default:
            rc = FLASH_HAL_PARAM(RET_R_INVALID_ARG);
            break;
    }
    /* 返回工作的结果 */
    if (s_flash.cancel_requested != 0u) {
        flash_finish_job(HAL_FLASH_JOB_CANCELED);
    } else if (ret_is_ok(rc)) {
        flash_finish_job(HAL_FLASH_JOB_OK);
    } else {
        flash_finish_job(HAL_FLASH_JOB_FAILED);
    }
}
/**
 * @brief 同步读取数据
 * @param addr 基地址
 * @param dst 存储的地址
 * @param len 读取大小
 * @return 状态码
 */
ret_code_t hal_flash_read_sync(uint32_t addr, void *dst, uint32_t len) {
    REQUIRE_RET(s_flash.initialized, FLASH_HAL_STATE(RET_R_NOT_READY));
    REQUIRE_RET(!OSAL_in_isr(), FLASH_HAL_STATE(RET_R_STATE_ERR));
    REQUIRE_RET((dst != NULL) && (len != 0u), FLASH_HAL_PARAM(RET_R_INVALID_ARG));
    if (flash_lock(OSAL_WAIT_FOREVER) != RET_OK) return FLASH_HAL_STATE(RET_R_BUSY);
    /* 同步路径与异步路径共用状态机，避免同一模块多路访问。 */
    if (s_flash.status != HAL_FLASH_STATUS_IDLE) {
        flash_unlock();
        return FLASH_HAL_STATE(RET_R_BUSY);
    }
    /* 更新状态机 */
    s_flash.status           = HAL_FLASH_STATUS_BUSY;
    s_flash.job_result       = HAL_FLASH_JOB_PENDING;
    s_flash.dispatching      = true; /* 防止被 work函数进行处理 */
    s_flash.cancel_requested = 0u;
    flash_unlock();
    /* 读取 数据 */
    const ret_code_t rc = flash_read_direct(addr, dst, len);
    /* 更新状态机 */
    if (flash_lock(OSAL_WAIT_FOREVER) == RET_OK) {
        s_flash.status           = HAL_FLASH_STATUS_IDLE;
        s_flash.job_result       = ret_is_ok(rc) ? HAL_FLASH_JOB_OK : HAL_FLASH_JOB_FAILED;
        s_flash.dispatching      = false;
        s_flash.cancel_requested = 0u;
        flash_unlock();
    }
    return rc;
}
/**
 * @brief 阻塞擦写数据
 * @param addr 基地址
 * @param len 长度
 * @return 状态码
 */
ret_code_t hal_flash_erase_sync(uint32_t addr, uint32_t len) {
    /* 参数检查 */
    REQUIRE_RET(s_flash.initialized, FLASH_HAL_STATE(RET_R_NOT_READY));
    REQUIRE_RET(!OSAL_in_isr(), FLASH_HAL_STATE(RET_R_STATE_ERR));
    REQUIRE_RET(len != 0u, FLASH_HAL_PARAM(RET_R_RANGE_ERR));
    if (flash_lock(OSAL_WAIT_FOREVER) != RET_OK) return FLASH_HAL_STATE(RET_R_BUSY);
    if (s_flash.status != HAL_FLASH_STATUS_IDLE) {
        flash_unlock();
        return FLASH_HAL_STATE(RET_R_BUSY);
    }
    /* 更新状态机 */
    s_flash.status           = HAL_FLASH_STATUS_BUSY;
    s_flash.job_result       = HAL_FLASH_JOB_PENDING;
    s_flash.dispatching      = true;
    s_flash.cancel_requested = 0u;
    flash_unlock();
    /* 擦除数据 */
    const ret_code_t rc = flash_erase_direct(addr, len);
    /* 更新状态机 */
    if (flash_lock(OSAL_WAIT_FOREVER) == RET_OK) {
        s_flash.status           = HAL_FLASH_STATUS_IDLE;
        s_flash.job_result       = ret_is_ok(rc) ? HAL_FLASH_JOB_OK : HAL_FLASH_JOB_FAILED;
        s_flash.dispatching      = false;
        s_flash.cancel_requested = 0u;
        flash_unlock();
    }
    return rc;
}
/**
 * @brief 阻塞写数据到 flash
 * @param addr 基地址
 * @param src 数据源地址
 * @param len 长度
 * @return
 */
ret_code_t hal_flash_write_sync(uint32_t addr, const void *src, uint32_t len) {
    /* 参数检查 */
    REQUIRE_RET(s_flash.initialized, FLASH_HAL_STATE(RET_R_NOT_READY));
    REQUIRE_RET(!OSAL_in_isr(), FLASH_HAL_STATE(RET_R_STATE_ERR));
    REQUIRE_RET((src != NULL) && (len != 0u), FLASH_HAL_PARAM(RET_R_INVALID_ARG));
    if (flash_lock(OSAL_WAIT_FOREVER) != RET_OK) return FLASH_HAL_STATE(RET_R_BUSY);
    if (s_flash.status != HAL_FLASH_STATUS_IDLE) {
        flash_unlock();
        return FLASH_HAL_STATE(RET_R_BUSY);
    }
    /* 更新状态机 */
    s_flash.status           = HAL_FLASH_STATUS_BUSY;
    s_flash.job_result       = HAL_FLASH_JOB_PENDING;
    s_flash.dispatching      = true;
    s_flash.cancel_requested = 0u;
    flash_unlock();
    /* 写入数据 */
    const ret_code_t rc = flash_write_direct(addr, src, len);
    /* 更新状态机 */
    if (flash_lock(OSAL_WAIT_FOREVER) == RET_OK) {
        s_flash.status           = HAL_FLASH_STATUS_IDLE;
        s_flash.job_result       = ret_is_ok(rc) ? HAL_FLASH_JOB_OK : HAL_FLASH_JOB_FAILED;
        s_flash.dispatching      = false;
        s_flash.cancel_requested = 0u;
        flash_unlock();
    }
    return rc;
}
/**
 *
 * @param addr
 * @param src
 * @param len
 * @return
 */
ret_code_t hal_flash_compare_sync(uint32_t addr, const void *src, uint32_t len) {
    /* 参数检查 */
    REQUIRE_RET(s_flash.initialized, FLASH_HAL_STATE(RET_R_NOT_READY));
    REQUIRE_RET(!OSAL_in_isr(), FLASH_HAL_STATE(RET_R_STATE_ERR));
    REQUIRE_RET((src != NULL) && (len != 0u), FLASH_HAL_PARAM(RET_R_INVALID_ARG));
    if (flash_lock(OSAL_WAIT_FOREVER) != RET_OK) return FLASH_HAL_STATE(RET_R_BUSY);
    if (s_flash.status != HAL_FLASH_STATUS_IDLE) {
        flash_unlock();
        return FLASH_HAL_STATE(RET_R_BUSY);
    }
    /* 状态机 */
    s_flash.status           = HAL_FLASH_STATUS_BUSY;
    s_flash.job_result       = HAL_FLASH_JOB_PENDING;
    s_flash.dispatching      = true;
    s_flash.cancel_requested = 0u;
    flash_unlock();

    const ret_code_t rc = flash_compare_direct(addr, src, len);
    /* 更新状态机  */
    if (flash_lock(OSAL_WAIT_FOREVER) == RET_OK) {
        s_flash.status           = HAL_FLASH_STATUS_IDLE;
        s_flash.job_result       = ret_is_ok(rc) ? HAL_FLASH_JOB_OK : HAL_FLASH_JOB_FAILED;
        s_flash.dispatching      = false;
        s_flash.cancel_requested = 0u;
        flash_unlock();
    }
    return rc;
}

ret_code_t hal_flash_blank_check_sync(uint32_t addr, uint32_t len, bool *out) {
    /* 参数检查 */
    REQUIRE_RET(s_flash.initialized, FLASH_HAL_STATE(RET_R_NOT_READY));
    REQUIRE_RET(!OSAL_in_isr(), FLASH_HAL_STATE(RET_R_STATE_ERR));
    REQUIRE_RET((out != NULL) && (len != 0u), FLASH_HAL_PARAM(RET_R_INVALID_ARG));
    if (flash_lock(OSAL_WAIT_FOREVER) != RET_OK) return FLASH_HAL_STATE(RET_R_BUSY);
    if (s_flash.status != HAL_FLASH_STATUS_IDLE) {
        flash_unlock();
        return FLASH_HAL_STATE(RET_R_BUSY);
    }
    /* 更新状态机 */
    s_flash.status           = HAL_FLASH_STATUS_BUSY;
    s_flash.job_result       = HAL_FLASH_JOB_PENDING;
    s_flash.dispatching      = true;
    s_flash.cancel_requested = 0u;
    flash_unlock();

    const ret_code_t rc = flash_blank_check_direct(addr, len, out);
    /* 更新状态机 */
    if (flash_lock(OSAL_WAIT_FOREVER) == RET_OK) {
        s_flash.status           = HAL_FLASH_STATUS_IDLE;
        s_flash.job_result       = ret_is_ok(rc) ? HAL_FLASH_JOB_OK : HAL_FLASH_JOB_FAILED;
        s_flash.dispatching      = false;
        s_flash.cancel_requested = 0u;
        flash_unlock();
    }
    return rc;
}

ret_code_t hal_flash_is_erased(uint32_t addr, uint32_t len, bool *out) {
    return hal_flash_blank_check_sync(addr, len, out);
}

#else

ret_code_t hal_flash_init(const hal_flash_cfg_t *cfg) {
    (void)cfg;
    return FLASH_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_flash_get_status(hal_flash_status_t *out) {
    if (out != NULL) *out = HAL_FLASH_STATUS_UNINIT;
    return FLASH_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_flash_get_job_result(hal_flash_job_result_t *out) {
    if (out != NULL) *out = HAL_FLASH_JOB_NONE;
    return FLASH_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_flash_set_mode(hal_flash_mode_t mode) {
    (void)mode;
    return FLASH_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_flash_cancel(void) {
    return FLASH_HAL_PARAM(RET_R_UNSUPPORTED);
}

void hal_flash_main_function(void) {
}

ret_code_t hal_flash_get_info(hal_flash_info_t *out) {
    (void)out;
    return FLASH_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_flash_get_region(uint32_t addr, hal_flash_region_t *out) {
    (void)addr;
    (void)out;
    return FLASH_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_flash_read(uint32_t addr, void *dst, uint32_t len) {
    (void)addr;
    (void)dst;
    (void)len;
    return FLASH_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_flash_erase(uint32_t addr, uint32_t len) {
    (void)addr;
    (void)len;
    return FLASH_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_flash_write(uint32_t addr, const void *src, uint32_t len) {
    (void)addr;
    (void)src;
    (void)len;
    return FLASH_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_flash_compare(uint32_t addr, const void *src, uint32_t len) {
    (void)addr;
    (void)src;
    (void)len;
    return FLASH_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_flash_blank_check(uint32_t addr, uint32_t len, bool *out) {
    (void)addr;
    (void)len;
    (void)out;
    return FLASH_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_flash_read_sync(uint32_t addr, void *dst, uint32_t len) {
    (void)addr;
    (void)dst;
    (void)len;
    return FLASH_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_flash_erase_sync(uint32_t addr, uint32_t len) {
    (void)addr;
    (void)len;
    return FLASH_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_flash_write_sync(uint32_t addr, const void *src, uint32_t len) {
    (void)addr;
    (void)src;
    (void)len;
    return FLASH_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_flash_compare_sync(uint32_t addr, const void *src, uint32_t len) {
    (void)addr;
    (void)src;
    (void)len;
    return FLASH_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_flash_blank_check_sync(uint32_t addr, uint32_t len, bool *out) {
    (void)addr;
    (void)len;
    (void)out;
    return FLASH_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_flash_is_erased(uint32_t addr, uint32_t len, bool *out) {
    (void)addr;
    (void)len;
    (void)out;
    return FLASH_HAL_PARAM(RET_R_UNSUPPORTED);
}

#endif
