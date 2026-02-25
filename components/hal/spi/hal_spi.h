#ifndef HAL_SPI_H
#define HAL_SPI_H

#include <stdbool.h>
#include <stdint.h>

#include "ret_code.h"

#ifdef __cplusplus
extern "C" {
#endif
/* clang-format off */
/* ========================================================================================= *
 *  SPI 事务与硬件流安全配置表                                 *
 * ========================================================================================= *
 * @verbatim
 * | 设备配置 (is_master, dir)       | 请求类型(tx/rx)   | transceive | stream_start | 关键底层要求
 * |--------------------------------|-----------------|------------|--------------|----------------------------------------------------------
 * | Master/Slave + 2LINES          | TXRX (tx && rx) |   ✅ 支持  |   ✅ 支持    | DMA 需 hdma_tx+hdma_rx；硬件流时两路均需 DMA_CIRCULAR
 * | Master/Slave + 2LINES          | TX   (tx && !rx)|   ✅ 支持  |   ✅ 支持    | DMA 需 hdma_tx；硬件流时 hdma_tx 需 DMA_CIRCULAR
 * | Master       + 2LINES          | RX   (!tx && rx)|   ✅ 支持  |   ✅ 支持    | DMA 必须有 hdma_rx 且必须有 hdma_tx (发伪数据挤时钟)
 * | Slave        + 2LINES          | RX   (!tx && rx)|   ✅ 支持  |   ✅ 支持    | DMA 需 hdma_rx；硬件流时 hdma_rx 需 DMA_CIRCULAR
 * | Master/Slave + 2LINES_RXONLY   | TX   (tx && !rx)|   ❌ 拒绝  |   ❌ 拒绝    | 硬件方向本身禁止仅发送
 * | Master/Slave + 2LINES_RXONLY   | RX   (!tx && rx)|   ✅ 支持  | Slave可/主拒 | Master 状态下，软/硬流模式的 RX-only 均被拒绝 (防时钟跑飞)
 * | Master/Slave + 1LINE           | TX   (tx && !rx)|   ✅ 支持  |   ✅ 支持    | DMA 需 hdma_tx；硬件流时 hdma_tx 需 DMA_CIRCULAR
 * | Master/Slave + 1LINE           | RX   (!tx && rx)|   ✅ 支持  | Slave可/主拒 | Master 状态下，软/硬流模式的 1LINE RX 均被拒绝
 * | 任意方向 != 2LINES              | TXRX (tx && rx) |   ❌ 拒绝  |   ❌ 拒绝    | TXRX 全双工操作只允许在 2LINES 模式下进行
 * @endverbatim
 * * @note API 语义补充:
 * 1. [hal_spi_transceive]   : 一次性事务，禁止传 HAL_SPI_XFER_HW_STREAM。
 * 2. [hal_spi_stream_start] : 启动硬件流(DMA Circular)，回调触发 STREAM_HALF/FULL。
 * 3. [hal_spi_stream_stop]  : 停止 DMA (可选 DeInit)，停止动作本身不触发 DONE 事件。
 * 4. [hal_spi_abort]        : 强制中止当前设备事务（普通异步/硬件流），可选 DeInit SPI。
 * 5. [XFER_STREAM/END 标志] : 代表“软件流分包语义”(期间不断片选)，非硬件 Circular。
 *
 * @warning 调用建议:
 * - 主机硬件流（发或收），一律优先配为 2LINES 全双工模式。
 * - 主机仅接收的硬件流（如ADC），必须开双路 DMA Circular。
 * - 关闭DMA、SPI、中断使能、以及内部信息 调用顺序 hal_spi_stream_stop(dev, true) -> hal_spi_dev_detach(dev) -> hal_spi_bus_close(bus)
 * ========================================================================================= */
/* clang-format on */

typedef struct hal_spi_bus hal_spi_bus_t; /* 不透明 */
typedef struct hal_spi_dev hal_spi_dev_t; /* 不透明 */
/* 板级id */
typedef enum {
    HAL_SPI_BUS1    = 0,
    HAL_SPI_BUS2    = 1,
    HAL_SPI_BUS3    = 2,
    HAL_SPI_BUS4    = 3,
    HAL_SPI_BUS_MAX = 4, /* 用于判断 SPI总数 以及后面的资源池大小 */
} hal_spi_id_t;
/* SPI模式 */
typedef enum {
    HAL_SPI_MODE0 = 0,
    HAL_SPI_MODE1,
    HAL_SPI_MODE2,
    HAL_SPI_MODE3,
} hal_spi_mode_t;

/* 大小端序 */
typedef enum {
    HAL_SPI_BITORDER_MSB = 0,
    HAL_SPI_BITORDER_LSB = 1,
} hal_spi_bitorder_t;

/* 数据位宽 */
typedef enum {
    HAL_SPI_FRAME_8  = 8,
    HAL_SPI_FRAME_16 = 16, /* 此选项需要 确保数据发送和接收的地址16位对齐 */
} hal_spi_frame_bits_t;

/* 总线配置 */
typedef struct {
    uint8_t bus_id;      /* 板级映射 id */
    bool use_dma;        /* 是否启用dma */
    bool use_irq;        /* 是否启用中断 */
    uint32_t default_hz; /* 默认总线配置的速率 */
} hal_spi_bus_cfg_t;     /* 总线配置  板级id、irq、DMA */

/* 片选模式 */
typedef enum {
    HAL_SPI_CS_GPIO = 0,
    HAL_SPI_CS_HW   = 1,
} hal_spi_cs_type_t;

typedef enum {
    HAL_SPI_DIR_2LINES        = 0, /* 全双工 */
    HAL_SPI_DIR_2LINES_RXONLY = 1, /* 双线中一根线闲置一根线仅接收 */
    HAL_SPI_DIR_LINE          = 2, /* 单线半双工 */
} hal_spi_dir_t;
/* SPI配置 */
typedef struct {
    hal_spi_mode_t mode;             /* SPI通信模式 */
    hal_spi_bitorder_t bit_order;    /* 大小端序 */
    hal_spi_frame_bits_t frame_bits; /* 数据位宽 */
    uint32_t max_hz;                 /* 设置最大通信频率 */
    hal_spi_cs_type_t cs_type;       /* 片选类型 */
    uint32_t cs_gpio_id;             /* 片选 GPIO id */
    bool cs_active_low;              /* 片选信号的活动电平 */
    uint16_t cs_setup_us;            /* 片选建立时间 */
    uint16_t cs_hold_us;             /* 片选保持时间 */
    bool use_ti_mode;                /* 是否启用德州模式 */
    bool use_crc;                    /* 是否启用 crc计算 */
    bool is_master;                  /* 主从模式选择 */
    hal_spi_dir_t dir;               /* 通信双工选择 */
} hal_spi_dev_cfg_t;

typedef enum {
    HAL_SPI_XFER_NONE       = 0,         /* 标准 */
    HAL_SPI_XFER_KEEP_CS    = (1u << 0), /* 保持 不要释放 */
    HAL_SPI_XFER_NO_CS      = (1u << 1), /* 当前事务不控制片选 */
    HAL_SPI_XFER_STREAM     = (1u << 2), /* 软件流（分包）DMA必须为 normal */
    HAL_SPI_XFER_STREAM_END = (1u << 3), /* 软件流结束（最后一包） */
    HAL_SPI_XFER_HW_STREAM  = (1u << 4), /* 硬件流（DMA circular） */
} hal_spi_xfer_flags_t;

/* SPI 事件类型（用于异步传输完成通知） */
typedef enum {
    HAL_SPI_EVT_DONE        = 1, /* done.bytes: 本次完成字节数 */
    HAL_SPI_EVT_ERROR       = 2, /* err.rc: 统一 ret_code 错误码 */
    HAL_SPI_EVT_STREAM_HALF = 3, /* stream.bytes: DMA 半满进度 */
    HAL_SPI_EVT_STREAM_FULL = 4, /* stream.bytes: DMA 全满进度 */
} hal_spi_evt_type_t;
/* eventbus 语义：
 * - DONE/STREAM_*: payload_u32 = bytes
 * - ERROR:         payload_u32 = ret_code_t
 * - key: [31:24]bus_id [23:16]cs_type [15:0]cs_gpio_id(低16位) */

/* SPI 事件载体 */
typedef struct {
    hal_spi_evt_type_t type;
    union {
        struct {
            uint32_t bytes;
        } done;
        struct {
            ret_code_t rc;
        } err;
        struct {
            uint32_t bytes;
        } stream;
    };
} hal_spi_event_t;

/* SPI 设备事件回调
 * @note 当 CFG_PARAM_SPI_CB_IN_ISR=0 时，ISR 上下文不会直调该回调。
 *       建议业务侧通过 eventbus 订阅 SPI 事件。 */
typedef void (*hal_spi_evt_cb_t)(void *user, const hal_spi_event_t *evt);

/* 数据传输载体 */
typedef struct {
    const void *tx;      /* 发送数据源地址，可为 NULL */
    void *rx;            /* 接收数据目的地址，可为 NULL */
    uint32_t len;        /* 数据大小 */
    uint32_t timeout_ms; /* 发起路径锁超时（非阻塞模式下不是传输完成超时） */
    uint32_t flags;      /* 片选线传输后的处理 */
} hal_spi_xfer_t;

/**
 * @brief 打开 SPI 总线
 * @param cfg     总线配置（bus_id、DMA/IRQ、默认频率）
 * @param out_bus 返回总线句柄
 * @return RET_OK 或错误码
 */
ret_code_t hal_spi_bus_open(const hal_spi_bus_cfg_t *cfg, hal_spi_bus_t **out_bus);

/**
 * @brief 关闭 SPI 总线 DeInit & 中断使能
 * @param bus 总线句柄
 * @return RET_OK 或错误码
 * @note 若仍有设备挂载或事务进行中会返回 BUSY
 */
ret_code_t hal_spi_bus_close(hal_spi_bus_t *bus);

/**
 * @brief 挂载 SPI 设备到总线
 * @param bus     总线句柄
 * @param cfg     设备配置（模式、位宽、频率、片选策略）
 * @param out_dev 返回设备句柄
 * @return RET_OK 或错误码
 */
ret_code_t hal_spi_dev_attach(hal_spi_bus_t *bus, const hal_spi_dev_cfg_t *cfg,
                              hal_spi_dev_t **out_dev);

/**
 * @brief 从总线解绑 SPI 设备 关闭设备的 cs GPIO
 * @param dev 设备句柄
 * @return RET_OK 或错误码
 */
ret_code_t hal_spi_dev_detach(hal_spi_dev_t *dev);

/**
 * @brief 注册设备事件回调
 * @param dev  设备句柄
 * @param cb   回调函数
 * @param user 用户上下文
 * @return RET_OK 或错误码
 * @note SPI 事件默认会投递到 eventbus（CFG_PARAM_SPI_EVT_USE_EVENTBUS=1）。
 */
ret_code_t hal_spi_dev_set_evt_cb(hal_spi_dev_t *dev, hal_spi_evt_cb_t cb, void *user);

/**
 * @brief 发起一次异步事务（非阻塞）
 * @param dev  设备句柄
 * @param xfer 事务参数
 * @return RET_OK 或错误码
 * @note 成功返回仅表示“发起成功”，完成结果通过事件回调上报
 */
ret_code_t hal_spi_transceive(hal_spi_dev_t *dev, const hal_spi_xfer_t *xfer);

/**
 * @brief 同步收发（阻塞到事务完成或超时）
 * @param dev     设备句柄
 * @param tx      发送缓存，可为 NULL（纯接收）
 * @param rx      接收缓存，可为 NULL（纯发送）
 * @param len     传输字节数
 * @param wait_ms 等待完成超时（ms），传 0 表示永久等待
 * @return RET_OK 或错误码
 * @note 该接口仅封装一次性事务，不包含 stream 语义
 */
ret_code_t hal_spi_transceive_sync(hal_spi_dev_t *dev, const void *tx, void *rx, uint32_t len,
                                   uint32_t wait_ms);

/**
 * @brief 同步发送（阻塞到发送完成或超时）
 * @param dev     设备句柄
 * @param tx      发送缓存地址
 * @param len     发送字节数
 * @param wait_ms 等待完成超时（ms），传 0 表示永久等待
 * @return RET_OK 或错误码
 */
ret_code_t hal_spi_send_sync(hal_spi_dev_t *dev, const void *tx, uint32_t len, uint32_t wait_ms);

/**
 * @brief 同步接收（阻塞到接收完成或超时）
 * @param dev     设备句柄
 * @param rx      接收缓存地址
 * @param len     接收字节数
 * @param wait_ms 等待完成超时（ms），传 0 表示永久等待
 * @return RET_OK 或错误码
 */
ret_code_t hal_spi_recv_sync(hal_spi_dev_t *dev, void *rx, uint32_t len, uint32_t wait_ms);

/**
 * @brief 启动硬件流事务（DMA Circular）
 * @param dev  设备句柄
 * @param xfer 事务参数
 * @return RET_OK 或错误码
 */
ret_code_t hal_spi_stream_start(hal_spi_dev_t *dev, const hal_spi_xfer_t *xfer);

/**
 * @brief 强制中止当前设备正在进行的事务
 * @param dev         设备句柄
 * @param disable_spi true: 中止后反初始化 SPI；false: 仅中止事务
 * @return RET_OK 或错误码
 * @note 仅能中止“当前活跃设备”的事务；若总线忙于其他设备会返回 BUSY
 * 非 NO_CS 会释放 cs 底层调用  HAL_SPI_Abort （暂时）暂停DMA/IT 不会关闭NVIC 只能close 函数关闭
 */
ret_code_t hal_spi_abort(hal_spi_dev_t *dev, bool disable_spi);

/**
 * @brief 停止硬件流事务 & 停止DMA 可选DeInit SPI
 * @param dev         设备句柄
 * @param disable_spi true: 停止后反初始化 SPI；false: 仅停止流
 * @return RET_OK 或错误码
 */
ret_code_t hal_spi_stream_stop(hal_spi_dev_t *dev, bool disable_spi);
/**
 * @brief 内部错误弱钩子函数
 * @param rc_port port 错误码
 * @param rc_hal  hal 错误码
 * @param api     api名称
 * @param arg0   参数1
 * @param arg1   参数2
 */
void hal_spi_on_port_error(ret_code_t rc_port, ret_code_t rc_hal, const char *api, uint32_t arg0,
                           uint32_t arg1);
#ifdef __cplusplus
}
#endif
#endif
