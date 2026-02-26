#ifndef HAL_I2C_PORT_H
#define HAL_I2C_PORT_H

#include "APP_config.h"
#include "stm32_hal_config.h"

#if defined(CFG_TARGET_PLATFORM_STM32_HAL) && defined(CFG_FEAT_HAL_I2C) && (CFG_FEAT_HAL_I2C == 1)
#include <stdbool.h>
#include <stdint.h>

#include "hal_i2c.h"
#include "ret_code.h"
#include "stm32_i2c_bsp.h"

#ifndef CFG_PARAM_I2C_PORT_USE_LOCAL_HAL_CALLBACKS
#define CFG_PARAM_I2C_PORT_USE_LOCAL_HAL_CALLBACKS 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================================= *
 * I2C port 层语义说明
 * 1. port 层只负责“硬件配置 + 异步发起 + HAL 回调桥接”，不做业务级阻塞等待。
 * 2. 当前实现支持 DMA 或 IT 任一异步路径；两者都关闭会返回 UNSUPPORTED。
 * 3. 当前实现只支持主机事务；从机事务在 HAL 层会被拒绝。
 * 4. 事件回调含两类：
 *    - HAL_I2C_PORT_EVT_DONE : rc_port=RET_OK，tx/rx 字节数有效
 *    - HAL_I2C_PORT_EVT_ERROR: rc_port 为 port 错误码，tx/rx 字节数为 0
 * ========================================================================================= */

typedef enum {
    HAL_I2C_PORT_EVT_DONE  = 1,
    HAL_I2C_PORT_EVT_ERROR = 2,
} hal_i2c_port_evt_type_t;

typedef struct {
    hal_i2c_port_evt_type_t type;
    ret_code_t rc_port;
    uint32_t tx_bytes;
    uint32_t rx_bytes;
} hal_i2c_port_evt_t;

typedef void (*hal_i2c_port_evt_cb_t)(void *user, const hal_i2c_port_evt_t *evt);

typedef struct {
    stm32_i2c_bsp_t bsp; /* 板级资源映射 */
    bool use_dma;        /* 该总线是否启用 DMA 异步路径 */
    bool use_irq;        /* 该总线是否启用 IT 异步路径 */

    bool cfg_cache_valid;             /* 最近一次 apply 配置是否有效 */
    hal_i2c_addr_mode_t cur_addr_mode; /* 当前缓存地址模式 */
    uint32_t cur_hz;                  /* 当前缓存频率 */
    bool cur_no_stretch;              /* 当前缓存 no-stretch */
    bool cur_general_call;            /* 当前缓存 general-call */

    bool opened;               /* port 是否已打开 */
    volatile uint8_t xfer_busy; /* 是否存在在途异步事务 */

    uint16_t active_dev_addr;             /* 当前活跃设备地址（HAL 格式） */
    uint32_t active_tx_len;               /* 当前活跃事务 tx 字节数 */
    uint32_t active_rx_len;               /* 当前活跃事务 rx 字节数 */
    const uint8_t *active_tx;             /* 当前活跃 tx 缓冲区 */
    uint8_t *active_rx;                   /* 当前活跃 rx 缓冲区 */

    hal_i2c_port_evt_cb_t evt_cb; /* port 事件回调 */
    void *evt_user;               /* 回调用户上下文 */
} hal_i2c_port_ctx_t;

/**
 * @brief 打开 I2C port
 * @param cfg 总线配置
 * @param out 返回 port 上下文
 * @return RET_OK 或错误码
 * @note 打开后会完成 BSP 资源关联与可选 NVIC 配置
 */
ret_code_t hal_i2c_port_open(const hal_i2c_bus_cfg_t *cfg, hal_i2c_port_ctx_t *out);

/**
 * @brief 关闭 I2C port
 * @param ctx port 句柄
 * @return RET_OK 或错误码
 * @note 若仍有事务进行中会返回 BUSY
 */
ret_code_t hal_i2c_port_close(hal_i2c_port_ctx_t *ctx);

/**
 * @brief 注册 port 事件回调
 * @param ctx  port 句柄
 * @param cb   回调函数
 * @param user 用户上下文
 * @return RET_OK 或错误码
 * @note cb 可为 NULL，用于关闭上报
 */
ret_code_t hal_i2c_port_set_evt_cb(hal_i2c_port_ctx_t *ctx, hal_i2c_port_evt_cb_t cb, void *user);

/**
 * @brief 应用设备配置到底层 I2C 外设
 * @param ctx             port 句柄
 * @param dev_cfg         设备配置
 * @param bus_default_hz  总线默认频率
 * @return RET_OK 或错误码
 * @note 若配置与缓存一致则直接复用，不重复 DeInit/Init
 */
ret_code_t hal_i2c_port_apply(hal_i2c_port_ctx_t *ctx, const hal_i2c_dev_cfg_t *dev_cfg,
                              uint32_t bus_default_hz);

/**
 * @brief 发起一次异步事务（发起成功即返回）
 * @param ctx     port 句柄
 * @param dev_cfg 设备配置
 * @param xfer    事务参数
 * @return RET_OK 或错误码
 * @note tx/rx 同时非空、NO_STOP 等不支持组合会直接拒绝
 */
ret_code_t hal_i2c_port_xfer(hal_i2c_port_ctx_t *ctx, const hal_i2c_dev_cfg_t *dev_cfg,
                             const hal_i2c_xfer_t *xfer);

/**
 * @brief 强制中止当前事务
 * @param ctx         port 句柄
 * @param disable_i2c true: 中止后反初始化 I2C；false: 仅中止事务
 * @return RET_OK 或错误码
 * @note 中止成功后会清理活跃事务上下文
 */
ret_code_t hal_i2c_port_abort(hal_i2c_port_ctx_t *ctx, bool disable_i2c);

/**
 * @brief Master TX 完成钩子
 * @param hi2c HAL I2C 句柄
 */
void hal_i2c_port_master_tx_cplt_hook(I2C_HandleTypeDef *hi2c);
/**
 * @brief Master RX 完成钩子
 * @param hi2c HAL I2C 句柄
 */
void hal_i2c_port_master_rx_cplt_hook(I2C_HandleTypeDef *hi2c);
/**
 * @brief Error 钩子
 * @param hi2c HAL I2C 句柄
 */
void hal_i2c_port_error_hook(I2C_HandleTypeDef *hi2c);

#ifdef __cplusplus
}
#endif

#endif

#endif /* HAL_I2C_PORT_H */
