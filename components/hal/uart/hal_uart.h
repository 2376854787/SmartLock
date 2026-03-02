#ifndef HAL_UART_H
#define HAL_UART_H

#include <stdbool.h>
#include <stdint.h>

#include "ret_code.h"

/**
 * @brief UART 逻辑端口标识
 */
typedef enum {
    HAL_UART_ID_0 = 0,
    HAL_UART_ID_1,
    HAL_UART_ID_2,
    HAL_UART_ID_3,
    HAL_UART_ID_4,
    HAL_UART_ID_MAX
} hal_uart_id_t;

/**
 * @brief UART 事件类型
 */
typedef enum {
    HAL_UART_EVT_RX      = 1,
    HAL_UART_EVT_TX_DONE = 2,
    HAL_UART_EVT_ERROR   = 3
} hal_uart_evt_type_t;

/**
 * @brief UART 数据位配置
 */
typedef enum {
    HAL_UART_DATA_BITS_8 = 0,
    HAL_UART_DATA_BITS_9
} hal_uart_data_bits_t;

/**
 * @brief UART 停止位配置
 */
typedef enum {
    HAL_UART_STOP_BITS_1 = 0,
    HAL_UART_STOP_BITS_2
} hal_uart_stop_bits_t;

/**
 * @brief UART 奇偶校验配置
 */
typedef enum {
    HAL_UART_PARITY_NONE = 0,
    HAL_UART_PARITY_EVEN,
    HAL_UART_PARITY_ODD
} hal_uart_parity_t;

/**
 * @brief UART 事件载荷
 */
typedef struct {
    hal_uart_evt_type_t type;
    union {
        struct {
            uint32_t bytes;
        } rx;
        struct {
            uint32_t bytes;
        } tx;
        struct {
            uint32_t flags;
        } err;
    };
} hal_uart_event_t;

/**
 * @brief UART 零拷贝可读窗口
 * @note 窗口仅在下一次 reserve/commit 之前有效，按只读语义使用
 */
typedef struct {
    const uint8_t* p1;
    uint32_t n1;
    const uint8_t* p2;
    uint32_t n2;
} hal_uart_read_span_t;

/**
 * @brief UART 事件回调
 */
typedef void (*hal_uart_evt_cb_t)(void* user, const hal_uart_event_t* evt);

/**
 * @brief UART 硬件配置
 * @note 只描述串口硬件参数，不承载接收缓冲策略
 */
typedef struct {
    uint32_t baud;
    hal_uart_data_bits_t data_bits;
    hal_uart_stop_bits_t stop_bits;
    hal_uart_parity_t parity;
    bool flow_ctrl;
} hal_uart_cfg_t;

typedef struct hal_uart hal_uart_t;

/**
 * @brief 初始化 UART 句柄并完成板级资源绑定
 * @param id 板级 UART 标识
 * @param cfg UART 硬件配置
 * @param out 返回 UART 句柄
 * @return RET_OK 或 HAL UART 统一错误码
 */
ret_code_t hal_uart_init(hal_uart_id_t id, const hal_uart_cfg_t* cfg, hal_uart_t** out);

/**
 * @brief 反初始化 UART 句柄并释放对应资源
 * @param h UART 句柄
 * @return RET_OK 或 HAL UART 统一错误码
 */
ret_code_t hal_uart_deinit(hal_uart_t* h);

/**
 * @brief 启动 UART 接收路径
 * @param h UART 句柄
 * @return RET_OK 或 HAL UART 统一错误码
 * @note 当前 STM32 port 默认使用 DMA 接收路径
 */
ret_code_t hal_uart_rx_start(hal_uart_t* h);

/**
 * @brief 从接收缓冲区拷贝读取数据
 * @param h UART 句柄
 * @param out 输出缓冲区
 * @param want 期望读取字节数
 * @param nread 实际读取字节数
 * @return RET_OK 或 HAL UART 统一错误码
 * @note 当前语义为尽力读取，允许返回少于 want 的字节数
 */
ret_code_t hal_uart_read(hal_uart_t* h, uint8_t* out, uint32_t want, uint32_t* nread);

/**
 * @brief 申请接收缓冲区中的可读窗口
 * @param h UART 句柄
 * @param want 期望读取字节数，传 0 表示尽可能多
 * @param out 返回窗口信息
 * @param nread 实际可读字节数
 * @return RET_OK 或 HAL UART 统一错误码
 */
ret_code_t hal_uart_read_reserve(hal_uart_t* h, uint32_t want, hal_uart_read_span_t* out,
                                 uint32_t* nread);

/**
 * @brief 提交已经消费的接收字节数
 * @param h UART 句柄
 * @param nread 已消费字节数
 * @return RET_OK 或 HAL UART 统一错误码
 */
ret_code_t hal_uart_read_commit(hal_uart_t* h, uint32_t nread);

/**
 * @brief 通过 UART 异步发送数据
 * @param h UART 句柄
 * @param buf 发送缓冲区
 * @param len 发送长度
 * @return RET_OK 或 HAL UART 统一错误码
 */
ret_code_t hal_uart_send_async(hal_uart_t* h, const uint8_t* buf, uint32_t len);

/**
 * @brief 注册 UART 事件回调
 * @param h UART 句柄
 * @param cb 回调函数
 * @param user 用户上下文
 * @return RET_OK 或 HAL UART 统一错误码
 */
ret_code_t hal_uart_set_evt_cb(hal_uart_t* h, hal_uart_evt_cb_t cb, void* user);

/**
 * @brief 内部错误弱钩子函数
 * @param rc_port port 错误码
 * @param rc_hal  hal 错误码
 * @param api     api 名称
 * @param arg0    参数 1
 * @param arg1    参数 2
 */
void hal_uart_on_port_error(ret_code_t rc_port, ret_code_t rc_hal, const char* api, uint32_t arg0,
                            uint32_t arg1);

#endif  // HAL_UART_H
