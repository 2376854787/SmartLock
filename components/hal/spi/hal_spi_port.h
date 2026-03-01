#ifndef HAL_SPI_PORT_H
#define HAL_SPI_PORT_H
#include "APP_config.h"
#include "stm32_hal_config.h"
#if defined(CFG_TARGET_PLATFORM_STM32_HAL) && defined(CFG_FEAT_HAL_SPI) && (CFG_FEAT_HAL_SPI == 1)
#include <stdbool.h>
#include <stdint.h>

#include "hal_spi.h"
#include "ret_code.h"
#include "stm32_spi_bsp.h"

/* SPI port ISR/回调默认由本文件对应的 c 实现提供，可在上层配置中覆盖为 0 */
#ifndef CFG_PARAM_SPI_PORT_USE_LOCAL_IRQ_HANDLER
#define CFG_PARAM_SPI_PORT_USE_LOCAL_IRQ_HANDLER 1
#endif
#ifndef CFG_PARAM_SPI_PORT_USE_LOCAL_HAL_CALLBACKS
#define CFG_PARAM_SPI_PORT_USE_LOCAL_HAL_CALLBACKS 1
#endif
#ifndef CFG_PARAM_SPI_STRICT_XFER_CHECK
#define CFG_PARAM_SPI_STRICT_XFER_CHECK 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_SPI_PORT_EVT_DONE = 1,
    HAL_SPI_PORT_EVT_ERROR,
    HAL_SPI_PORT_EVT_STREAM_HALF,
    HAL_SPI_PORT_EVT_STREAM_FULL,
} hal_spi_port_evt_type_t;

typedef struct {
    hal_spi_port_evt_type_t type;
    ret_code_t rc_port;
    uint32_t bytes;
} hal_spi_port_evt_t;

/* port 层事件回调：DONE/ERROR 用于普通异步，STREAM_* 用于硬件流模式 */
typedef void (*hal_spi_port_evt_cb_t)(void *user, const hal_spi_port_evt_t *evt);

typedef enum {
    HAL_SPI_PORT_REQ_NONE = 0,
    HAL_SPI_PORT_REQ_TX,
    HAL_SPI_PORT_REQ_RX,
    HAL_SPI_PORT_REQ_TXRX,
} hal_spi_port_req_t;

typedef struct {
    stm32_spi_bsp_t bsp; /* 板级资源映射 */
    bool use_dma;        /* 总线是否启用 DMA 传输 */
    bool use_irq;        /* 总线是否启用 SPI 中断 */
    /* =========== port 内部缓存 =========== */
    bool cfg_cache_valid;          /* 缓存配置有效性 */
    hal_spi_mode_t cur_mode;       /* 模式 */
    hal_spi_bitorder_t cur_order;  /* 大小端序 */
    hal_spi_frame_bits_t cur_bits; /* 位宽 */
    uint32_t cur_hz;               /* 当前的频率 */
    bool opened;                   /* 端口是否已初始化 */
    volatile uint8_t xfer_busy;    /* 当前是否正在异步传输 */
    /* ======== 当前异步传输上下文 =========== */
    hal_spi_port_req_t req_type;
    uint32_t active_len;
    const uint8_t *active_tx;
    uint8_t *active_rx;
    bool hw_stream_active; /* 当前是否处于硬件流模式 */
    /* ============= 回调配置 ============== */
    hal_spi_port_evt_cb_t evt_cb; /* 事件回调 */
    void *evt_user;               /* 回调用户上下文 */
} hal_spi_port_ctx_t;

/* 根据 bus 配置初始化底层端口，并完成 DMA/IRQ 相关硬件准备 */
ret_code_t hal_spi_port_init(const hal_spi_bus_cfg_t *cfg, hal_spi_port_ctx_t *out);
ret_code_t hal_spi_port_deinit(hal_spi_port_ctx_t *ctx);
ret_code_t hal_spi_port_set_evt_cb(hal_spi_port_ctx_t *ctx, hal_spi_port_evt_cb_t cb, void *user);

/* 应用 device 配置到硬件 */
ret_code_t hal_spi_port_apply(hal_spi_port_ctx_t *ctx, const hal_spi_dev_cfg_t *dev_cfg,
                              uint32_t bus_default_hz);

/* 硬件流模式（DMA circular）：通过半满/全满回调持续上报进度 */
ret_code_t hal_spi_port_stream_start(hal_spi_port_ctx_t *ctx, const hal_spi_xfer_t *xfer);
ret_code_t hal_spi_port_stream_stop(hal_spi_port_ctx_t *ctx, bool disable_spi);

/**
 * @brief 发起一次 port 层异步事务（发起成功后立即返回）
 * @param ctx  port 句柄
 * @param xfer 事务参数
 * @return RET_OK 或错误码
 */
ret_code_t hal_spi_port_xfer(hal_spi_port_ctx_t *ctx, const hal_spi_xfer_t *xfer);

/**
 * @brief 强制中止当前 port 层事务（普通异步/硬件流）
 * @param ctx         port 句柄
 * @param disable_spi true: 中止后反初始化 SPI；false: 仅中止事务
 * @return RET_OK 或错误码
 */
ret_code_t hal_spi_port_abort(hal_spi_port_ctx_t *ctx, bool disable_spi);

/**
 * @brief 等待底层 SPI 外设进入空闲态（如 BSY 清零）
 * @param ctx      port 句柄
 * @param spin_max 最大轮询次数（0 表示只检查一次）
 * @return RET_OK: 已空闲；其他为错误码（超时/状态异常）
 */
ret_code_t hal_spi_port_wait_idle(const hal_spi_port_ctx_t *ctx, uint32_t spin_max);

/* 当关闭本地 ISR/回调定义时，可在外部 ISR/HAL 回调中调用这些 hook */
void hal_spi_port_irq_dispatch_hook(IRQn_Type irqn);
void hal_spi_port_tx_half_cplt_hook(SPI_HandleTypeDef *hspi);
void hal_spi_port_rx_half_cplt_hook(SPI_HandleTypeDef *hspi);
void hal_spi_port_tx_cplt_hook(SPI_HandleTypeDef *hspi);
void hal_spi_port_rx_cplt_hook(SPI_HandleTypeDef *hspi);
void hal_spi_port_txrx_cplt_hook(SPI_HandleTypeDef *hspi);
void hal_spi_port_txrx_half_cplt_hook(SPI_HandleTypeDef *hspi);
void hal_spi_port_error_hook(SPI_HandleTypeDef *hspi);

#ifdef __cplusplus
}
#endif

#endif
#endif
