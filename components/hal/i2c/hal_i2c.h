#ifndef HAL_I2C_H
#define HAL_I2C_H

#include <stdbool.h>
#include <stdint.h>

#include "ret_code.h"

#ifdef __cplusplus
extern "C" {
#endif
/* clang-format off */
/* ========================================================================================= *
 *  I2C 事务能力与约束配置表
 * ========================================================================================= *
 * @verbatim
 * | 设备配置(is_master, addr_mode) | 请求类型(tx/rx)                | transceive | sync API | 关键约束
 * |--------------------------------|---------------------------------|------------|----------|----------------------------------------------------------|
 * | Master + 7bit/10bit            | TX only                        |   ✅ 支持  |   ✅ 支持 | 普通主机发送；send_sync 为该模式封装
 * | Master + 7bit/10bit            | RX only                        |   ✅ 支持  |   ✅ 支持 | 普通主机接收；recv_sync 为该模式封装
 * | Master + 7bit/10bit            | TXRX 同时非空                  |   ❌ 拒绝  |   ❌ 拒绝 | 当前版本不支持组合事务（写后读同一请求）
 * | Master + 7bit/10bit            | flags 含 NO_STOP               |   ❌ 拒绝  |   ❌ 拒绝 | 预留语义，当前版本未实现
 * | Slave + 任意地址模式            | 任意请求                         |   ❌ 拒绝  |   ❌ 拒绝 | 当前 HAL 仅支持主机模式，is_master 必须为 true
 * @endverbatim
 *
 * @note 事件语义：
 * 1. DONE 事件：done.tx_bytes / done.rx_bytes 表示本次事务完成字节数。
 * 2. ERROR 事件：err.rc 为统一 ret_code_t 错误码（已做 port->hal 映射）。
 * 3. HAL 只提供设备级回调；eventbus/queue/task-notify 等系统分发策略应由上层封装。
 *
 * @note API 语义补充：
 * 1. hal_i2c_transceive      : 一次异步事务，成功返回仅代表“已发起”。
 * 2. hal_i2c_transceive_sync : 异步发起 + 同步等待封装，等待完成/超时。
 * 3. hal_i2c_send_sync/recv_sync
 *    : 普通发送/接收快捷接口（内部封装 transceive_sync）。
 * 4. hal_i2c_abort           : 强制中止当前活跃设备事务，可选 DeInit I2C。
 *
 * @warning 调用建议：
 * - xfer.timeout_ms 是“发起路径锁超时”，不是“事务完成等待超时”。
 * - 同步等待超时由 *_sync 的 wait_ms 控制；wait_ms=0 表示永久等待。
 * - 推荐关闭顺序：hal_i2c_abort(可选) -> hal_i2c_dev_detach -> hal_i2c_bus_deinit。
 * ========================================================================================= */
/* clang-format on */

typedef struct hal_i2c_bus hal_i2c_bus_t; /* 不透明 */
typedef struct hal_i2c_dev hal_i2c_dev_t; /* 不透明 */

/* 板级 bus id（用于 BSP 资源映射） */
typedef enum {
    HAL_I2C_BUS1    = 0,
    HAL_I2C_BUS2    = 1,
    HAL_I2C_BUS3    = 2,
    HAL_I2C_BUS4    = 3,
    HAL_I2C_BUS_MAX = 4,
} hal_i2c_id_t;

/* 总线配置：决定总线实例、异步能力和默认频率上限 */
typedef struct {
    uint8_t bus_id;      /* 板级映射 id */
    bool use_dma;        /* 是否启用 DMA 传输 */
    bool use_irq;        /* 是否启用中断传输 */
    uint32_t default_hz; /* 总线默认频率 */
} hal_i2c_bus_cfg_t;

typedef enum {
    HAL_I2C_ADDR_7BIT  = 0, /* 7 位地址 */
    HAL_I2C_ADDR_10BIT = 1, /* 10 位地址 */
} hal_i2c_addr_mode_t;

/* 设备配置：每个逻辑设备独立维护地址和速率策略 */
typedef struct {
    uint16_t dev_addr; /* 7bit 或 10bit 地址（不含 R/W bit） */
    hal_i2c_addr_mode_t addr_mode;
    uint32_t max_hz;   /* 设备最高工作频率 */
    bool no_stretch;   /* 时钟拉伸控制 */
    bool general_call; /* General Call 响应 */
    bool is_master;    /* 当前 HAL 实现仅支持 master=true */
} hal_i2c_dev_cfg_t;

typedef enum {
    HAL_I2C_XFER_NONE    = 0,
    HAL_I2C_XFER_NO_STOP = (1u << 0), /* 预留标志，当前版本未实现 */
} hal_i2c_xfer_flags_t;

/* I2C 事务：当前仅支持普通 TX/RX（tx/rx 二选一） */
typedef struct {
    const void *tx;      /* 发送数据，可为 NULL */
    uint32_t tx_len;     /* 发送长度 */
    void *rx;            /* 接收数据，可为 NULL */
    uint32_t rx_len;     /* 接收长度 */
    uint32_t timeout_ms; /* 发起路径锁超时（非完成等待超时） */
    uint32_t flags;      /* 事务标志 */
} hal_i2c_xfer_t;

typedef enum {
    HAL_I2C_EVT_DONE  = 1, /* done.tx_bytes / done.rx_bytes */
    HAL_I2C_EVT_ERROR = 2, /* err.rc */
} hal_i2c_evt_type_t;

typedef struct {
    hal_i2c_evt_type_t type; /* 事件类型 */
    union {
        struct {
            uint32_t tx_bytes; /* 实际发送字节数 */
            uint32_t rx_bytes; /* 实际接收字节数 */
        } done;
        struct {
            ret_code_t rc; /* 统一 HAL 错误码 */
        } err;
    };
} hal_i2c_event_t;

/* I2C 设备事件回调
 * @note 当 CFG_PARAM_I2C_CB_IN_ISR=0 时，ISR 上下文不会直调该回调。 */
typedef void (*hal_i2c_evt_cb_t)(void *user, const hal_i2c_event_t *evt);

/**
 * @brief 初始化 I2C 总线
 * @param cfg     总线配置（bus_id、DMA/IRQ、默认频率）
 * @param out_bus 返回总线句柄
 * @return RET_OK 或错误码
 * @note 成功后会注册 port 回调并按需创建 lock（RTOS场景）
 */
ret_code_t hal_i2c_bus_init(const hal_i2c_bus_cfg_t *cfg, hal_i2c_bus_t **out_bus);

/**
 * @brief 反初始化 I2C 总线
 * @param bus 总线句柄
 * @return RET_OK 或错误码
 * @note 若仍有设备挂载或事务进行中会返回 BUSY
 * @note 反初始化后该 bus_id 可再次被 hal_i2c_bus_init 使用
 */
ret_code_t hal_i2c_bus_deinit(hal_i2c_bus_t *bus);

/**
 * @brief 挂载 I2C 设备到总线
 * @param bus     总线句柄
 * @param cfg     设备配置（地址、地址位宽、频率等）
 * @param out_dev 返回设备句柄
 * @return RET_OK 或错误码
 * @note 仅占用 HAL 设备资源槽，不直接触发硬件重配
 */
ret_code_t hal_i2c_dev_attach(hal_i2c_bus_t *bus, const hal_i2c_dev_cfg_t *cfg,
                              hal_i2c_dev_t **out_dev);

/**
 * @brief 从总线解绑 I2C 设备
 * @param dev 设备句柄
 * @return RET_OK 或错误码
 * @note 当前设备仍为活跃事务设备时会返回 BUSY
 */
ret_code_t hal_i2c_dev_detach(hal_i2c_dev_t *dev);

/**
 * @brief 注册 I2C 设备事件回调
 * @param dev  设备句柄
 * @param cb   回调函数
 * @param user 用户上下文
 * @return RET_OK 或错误码
 * @note eventbus/queue/task-notify 等系统分发应在上层基于该回调自行桥接
 */
ret_code_t hal_i2c_dev_set_evt_cb(hal_i2c_dev_t *dev, hal_i2c_evt_cb_t cb, void *user);

/**
 * @brief 发起一次异步事务（非阻塞）
 * @param dev  设备句柄
 * @param xfer 事务参数
 * @return RET_OK 或错误码
 * @note 成功返回仅表示“发起成功”，完成结果通过事件回调上报
 * @note 当前版本限制：tx/rx 不能同时非空；NO_STOP 不支持
 */
ret_code_t hal_i2c_transceive(hal_i2c_dev_t *dev, const hal_i2c_xfer_t *xfer);

/**
 * @brief 同步事务（阻塞到完成或超时）
 * @param dev     设备句柄
 * @param xfer    事务参数
 * @param wait_ms 等待完成超时（ms），0 表示永久等待
 * @return RET_OK 或错误码
 * @note 该接口内部仍走异步链路，由同步等待层收敛结果
 * @note 实现在 hal_i2c_sync.c（与异步核心解耦）
 */
ret_code_t hal_i2c_transceive_sync(hal_i2c_dev_t *dev, const hal_i2c_xfer_t *xfer,
                                   uint32_t wait_ms);

/**
 * @brief 同步发送（普通主机发送）
 * @param dev     设备句柄
 * @param tx      发送缓存
 * @param len     发送字节数
 * @param wait_ms 等待完成超时（ms），0 表示永久等待
 * @return RET_OK 或错误码
 */
ret_code_t hal_i2c_send_sync(hal_i2c_dev_t *dev, const void *tx, uint32_t len, uint32_t wait_ms);

/**
 * @brief 同步接收（普通主机接收）
 * @param dev     设备句柄
 * @param rx      接收缓存
 * @param len     接收字节数
 * @param wait_ms 等待完成超时（ms），0 表示永久等待
 * @return RET_OK 或错误码
 */
ret_code_t hal_i2c_recv_sync(hal_i2c_dev_t *dev, void *rx, uint32_t len, uint32_t wait_ms);

/**
 * @brief 中止当前设备事务
 * @param dev         设备句柄
 * @param disable_i2c true: 中止后反初始化 I2C；false: 仅中止事务
 * @return RET_OK 或错误码
 * @note 中止成功会统一上报 ERROR(RET_R_ABORTED)
 */
ret_code_t hal_i2c_abort(hal_i2c_dev_t *dev, bool disable_i2c);

/**
 * @brief 内部错误弱钩子（port->HAL 错误映射）
 */
void hal_i2c_on_port_error(ret_code_t rc_port, ret_code_t rc_hal, const char *api, uint32_t arg0,
                           uint32_t arg1);

#ifdef __cplusplus
}
#endif

#endif /* HAL_I2C_H */
