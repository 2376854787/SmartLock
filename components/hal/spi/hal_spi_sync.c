/**
 * @file hal_spi_sync.c
 * @brief SPI 同步接口封装（基于异步事件等待）
 */
#include "APP_config.h"
#if defined(CFG_FEAT_HAL_SPI) && (CFG_FEAT_HAL_SPI == 1)

#include "assert_cus.h"
#include "hal_spi.h"
#include "hal_spi_internal.h"
#include "hal_time.h"
#include "osal.h"

#define SPI_SYNC_RC_PARAM(reason_)   RET_MAKE_PARAM(RET_MOD_HAL, RET_SUB_HAL_SPI, (reason_))
#define SPI_SYNC_RC_STATE(reason_)   RET_MAKE_STATE(RET_MOD_HAL, RET_SUB_HAL_SPI, (reason_))
#define SPI_SYNC_RC_TIMEOUT(reason_) RET_MAKE_TIMEOUT(RET_MOD_HAL, RET_SUB_HAL_SPI, (reason_))
#define SPI_SYNC_RC_IO(reason_)      RET_MAKE_IO(RET_MOD_HAL, RET_SUB_HAL_SPI, (reason_))
#define SPI_SYNC_RC_RES(reason_)     RET_MAKE_RESOURCE(RET_MOD_HAL, RET_SUB_HAL_SPI, (reason_))

typedef struct {
    volatile uint8_t done; /* 是否收到终态事件（DONE/ERROR） */
    ret_code_t rc;         /* 终态结果码 */
    uint32_t bytes;        /* DONE 场景完成字节数 */
    osal_sem_t sem;        /* 同步等待信号量（RTOS 场景） */
    bool sem_valid;        /* 信号量是否创建成功 */
} spi_sync_wait_ctx_t;

/**
 * @brief 规范化同步等待超时参数
 * @param wait_ms 用户输入等待时间
 * @return wait_ms=0 时返回 OSAL_WAIT_FOREVER，否则返回原值
 */
static inline uint32_t spi_sync_norm_wait(uint32_t wait_ms) {
    return (wait_ms == 0u) ? OSAL_WAIT_FOREVER : wait_ms;
}

/**
 * @brief 将等待接口返回码映射为 HAL SPI 错误码
 * @param rc 等待接口返回码
 * @return HAL SPI 语义错误码
 */
static ret_code_t spi_map_wait_rc_to_hal(ret_code_t rc) {
    if (ret_is_ok(rc)) return RET_OK;

    if (ret_is_class(rc, RET_CLASS_PARAM)) {
        if (ret_is_reason(rc, RET_R_NULL_PTR))
            return SPI_SYNC_RC_PARAM(RET_R_NULL_PTR);
        else if (ret_is_reason(rc, RET_R_RANGE_ERR))
            return SPI_SYNC_RC_PARAM(RET_R_RANGE_ERR);
        else
            return SPI_SYNC_RC_PARAM(RET_R_INVALID_ARG);
    }

    if (ret_is_class(rc, RET_CLASS_TIMEOUT)) return SPI_SYNC_RC_TIMEOUT(RET_R_TIMEOUT);

    if (ret_is_class(rc, RET_CLASS_RESOURCE)) {
        if (ret_is_reason(rc, RET_R_NO_MEM))
            return SPI_SYNC_RC_RES(RET_R_NO_MEM);
        else
            return SPI_SYNC_RC_RES(RET_R_NO_RESOURCE);
    }

    if (ret_is_class(rc, RET_CLASS_STATE)) {
        if (ret_is_reason(rc, RET_R_BUSY))
            return SPI_SYNC_RC_STATE(RET_R_BUSY);
        else if (ret_is_reason(rc, RET_R_NOT_READY))
            return SPI_SYNC_RC_STATE(RET_R_NOT_READY);
        else
            return SPI_SYNC_RC_STATE(RET_R_STATE_ERR);
    }

    return SPI_SYNC_RC_IO(RET_R_IO);
}

/**
 * @brief 同步观察者回调（挂在异步事件链路上）
 * @param user 同步等待上下文
 * @param evt  SPI 事件
 * @note 仅 DONE/ERROR 会结束同步等待
 */
static void spi_sync_observer(void *user, const hal_spi_event_t *evt) {
    spi_sync_wait_ctx_t *ctx = (spi_sync_wait_ctx_t *)user;
    /* 参数与状态检查 */
    if (!ctx || !evt) return;
    if (ctx->done) return;

    /* 仅终态事件可以收敛同步等待 */
    if (evt->type == HAL_SPI_EVT_DONE) {
        ctx->rc    = RET_OK;
        ctx->bytes = evt->done.bytes;
        ctx->done  = 1u;
    } else if (evt->type == HAL_SPI_EVT_ERROR) {
        ctx->rc    = evt->err.rc;
        ctx->bytes = 0u;
        ctx->done  = 1u;
    } else {
        return;
    }

    /* 通知等待方（线程上下文 / ISR 上下文分别处理） */
    if (ctx->sem_valid) {
        if (OSAL_in_isr())
            (void)OSAL_sem_give_from_isr(ctx->sem);
        else
            (void)OSAL_sem_give(ctx->sem);
    }
}

/**
 * @brief 等待同步事务完成
 * @param ctx        同步等待上下文
 * @param wait_ms    等待超时（0=永久）
 * @param done_bytes 可选输出完成字节数
 * @return RET_OK 或错误码
 */
static ret_code_t spi_sync_wait_done(spi_sync_wait_ctx_t *ctx, uint32_t wait_ms, uint32_t *done_bytes) {
    if (!ctx) return SPI_SYNC_RC_PARAM(RET_R_NULL_PTR);
    const uint32_t wait_eff = spi_sync_norm_wait(wait_ms);

    /* 如果还未完成，进入等待路径 */
    if (!ctx->done) {
        if (ctx->sem_valid) {
            /* 优先信号量阻塞等待，减少 CPU 占用 */
            const ret_code_t sem_rc = OSAL_sem_take(ctx->sem, wait_eff);
            if (ret_is_err(sem_rc)) return spi_map_wait_rc_to_hal(sem_rc);
        } else {
            /* 无信号量时降级为轮询等待 */
            const uint32_t deadline_ms =
                (wait_eff == OSAL_WAIT_FOREVER) ? 0u : (hal_get_tick_ms() + wait_eff);
            while (!ctx->done) {
                if ((wait_eff != OSAL_WAIT_FOREVER) &&
                    HAL_TIME_AFTER_EQ(hal_get_tick_ms(), deadline_ms)) {
                    return SPI_SYNC_RC_TIMEOUT(RET_R_TIMEOUT);
                }
                if (OSAL_kernel_is_running())
                    (void)OSAL_delay_ms(1u);
                else
                    hal_time_delay_ms(1u);
            }
        }
    }

    /* 收敛等待结果 */
    if (!ctx->done) return SPI_SYNC_RC_STATE(RET_R_STATE_ERR);
    if (done_bytes) *done_bytes = ctx->bytes;
    return ctx->rc;
}

/**
 * @brief 同步事务通用实现（异步发起 + 等待完成）
 * @param dev     设备句柄
 * @param xfer    事务参数
 * @param wait_ms 等待超时（0=永久）
 * @return RET_OK 或错误码
 */
static ret_code_t spi_transceive_sync_common(hal_spi_dev_t *dev, const hal_spi_xfer_t *xfer,
                                             uint32_t wait_ms) {
    REQUIRE_RET(!OSAL_in_isr(), SPI_SYNC_RC_STATE(RET_R_STATE_ERR));
    REQUIRE_RET((dev != NULL) && (xfer != NULL), SPI_SYNC_RC_PARAM(RET_R_NULL_PTR));

    /* 构造本次调用私有等待上下文（生命周期仅本函数内） */
    spi_sync_wait_ctx_t ctx = {0};
    ctx.rc                  = SPI_SYNC_RC_STATE(RET_R_STATE_ERR);

    /* RTOS 场景尝试创建同步信号量（失败则后续走轮询降级） */
    if (OSAL_kernel_is_running() && ret_is_ok(OSAL_sem_create(&ctx.sem, "spi_sync_wait", 0u, 1u))) {
        ctx.sem_valid = true;
    }

    /* 先注册观察者，再发起异步事务，避免快速完成导致事件丢失 */
    ret_code_t rc = hal_spi_dev_set_sync_observer(dev, spi_sync_observer, &ctx);
    if (ret_is_err(rc)) goto cleanup;

    /* 发起异步事务 */
    rc = hal_spi_transceive(dev, xfer);
    if (ret_is_err(rc)) {
        (void)hal_spi_dev_set_sync_observer(dev, NULL, NULL);
        goto cleanup;
    }

    /* 等待 DONE/ERROR 终态并注销观察者 */
    rc = spi_sync_wait_done(&ctx, wait_ms, NULL);
    (void)hal_spi_dev_set_sync_observer(dev, NULL, NULL);

cleanup:
    /* 释放本次调用的同步资源 */
    if (ctx.sem_valid) (void)OSAL_sem_delete(ctx.sem);
    return rc;
}

/**
 * @brief 同步收发入口
 * @param dev     设备句柄
 * @param tx      发送缓存，可为 NULL
 * @param rx      接收缓存，可为 NULL
 * @param len     传输字节数
 * @param wait_ms 等待超时（0=永久）
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
 * @brief 同步发送快捷接口
 */
ret_code_t hal_spi_send_sync(hal_spi_dev_t *dev, const void *tx, uint32_t len, uint32_t wait_ms) {
    REQUIRE_RET(tx != NULL, SPI_SYNC_RC_PARAM(RET_R_NULL_PTR));
    return hal_spi_transceive_sync(dev, tx, NULL, len, wait_ms);
}

/**
 * @brief 同步接收快捷接口
 */
ret_code_t hal_spi_recv_sync(hal_spi_dev_t *dev, void *rx, uint32_t len, uint32_t wait_ms) {
    REQUIRE_RET(rx != NULL, SPI_SYNC_RC_PARAM(RET_R_NULL_PTR));
    return hal_spi_transceive_sync(dev, NULL, rx, len, wait_ms);
}

#endif
