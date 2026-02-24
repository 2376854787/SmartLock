#ifndef HAL_UART_H
#define HAL_UART_H

#include <stdbool.h>
#include <stdint.h>

#include "RingBuffer.h"
#include "ret_code.h"

/* 串口号定义 */
typedef enum {
    HAL_UART_ID_0 = 0,
    HAL_UART_ID_1,
    HAL_UART_ID_2,
    HAL_UART_ID_3,
    HAL_UART_ID_4,
    HAL_UART_ID_MAX  // 大小会影响后面实现静态串口数组的大小
} hal_uart_id_t;

/* UART 事件类型 */
typedef enum {
    HAL_UART_EVT_RX      = 1,  // rx.bytes: 新增字节数（上层随后 read() 拉取）
    HAL_UART_EVT_TX_DONE = 2,  // tx.bytes: 本次发送字节数
    HAL_UART_EVT_ERROR   = 3   // err.flags: 错误位图
} hal_uart_evt_type_t;

typedef enum { WORDLENGTH_8B = 0, WORDLENGTH_9B } uart_word_length_t;
typedef enum {
    STOPBITS_1 = 0,
    STOPBITS_2,
} uart_Stop_Bits;
/* 串口事件 */
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

/* 零拷贝读窗口（与 RingBufferSpan 对齐，按只读语义使用） */
typedef RingBufferSpan hal_uart_read_span_t;

/* 串口事件回调 */
typedef void (*hal_uart_evt_cb_t)(void* user, const hal_uart_event_t* evt);

typedef struct {
    uint32_t baud;                /* 波特率　*/
    uart_word_length_t data_bits; /* 数据位 */
    uart_Stop_Bits stop_bits;     /* 停止位 */
    uint32_t parity;              /* 奇偶校验 */
    bool flow_ctrl;               /* 硬件流控 true/false */
    bool isCompatible;            /* 用于 rb的对应参数 */
} hal_uart_cfg_t;
typedef struct hal_uart hal_uart_t; /* 实现文件具体内容 */
/**
 * @brief 接收回调函数指针
 * @param user_ctx
 * @param data 接收到的数据指针
 * @param len 数据长度
 */
typedef void (*hal_uart_rx_cb_t)(void* user_ctx, uint8_t* data, uint16_t len);

/**
 * @brief 将板级ID、参数映射后返回统一操作指针变量
 * @param id 板级串口id
 * @param cfg 串口配置
 * @param out 将底层内部静态串口配置变量的地址返回
 * @return 状态码
 * @note 必须在map文件 映射板级资源
 */
ret_code_t hal_uart_open(hal_uart_id_t id, const hal_uart_cfg_t* cfg, hal_uart_t** out);
/**
 * @brief 将串口配置、DMA、中断配置为默认状态
 * @param h 串口句柄
 * @return 状态码
 */
ret_code_t hal_uart_close(hal_uart_t* h);

/**
 * @brief 启动对应串口的接收功能 一般是 DMA + 半满　＋ 全满 ＋IDLE
 * @param h 串口句柄
 * @return 状态码
 * @note 可以通过宏配置接收配置 DMA/IT
 */
ret_code_t hal_uart_rx_start(hal_uart_t* h);
/**
 * @brief 在回调通知后
 * @param h 串口句柄
 * @param out 接收数据的容器地址
 * @param want 想要读取的字节数
 * @param nread 实际读取的字节数
 * @return 状态码
 */
ret_code_t hal_uart_read(hal_uart_t* h, uint8_t* out, uint32_t want, uint32_t* nread);

/**
 * @brief 申请串口接收缓冲区中的可读窗口（零拷贝）
 * @param h 串口句柄
 * @param want 想要读取的字节数。传 0 表示“尽可能多”。
 * @param out 返回窗口信息
 * @param nread 实际可读字节数
 * @return 状态码
 */
ret_code_t hal_uart_read_reserve(hal_uart_t* h, uint32_t want, hal_uart_read_span_t* out,
                                 uint32_t* nread);

/**
 * @brief 提交已经消费的接收字节数（与 hal_uart_read_reserve 配套）
 * @param h 串口句柄
 * @param nread 已消费字节数
 * @return 状态码
 */
ret_code_t hal_uart_read_commit(hal_uart_t* h, uint32_t nread);

/**
 * @brief 将=数据通过串口进行异步发送
 * @param h 串口句柄
 * @param buf 将要发送的数据地址
 * @param len 数据长度
 * @return 状态码
 */
ret_code_t hal_uart_send_async(hal_uart_t* h, const uint8_t* buf, uint32_t len);

/**
 *
 * @param h 句柄
 * @param cb 回调函数
 * @param user 用户上下文
 * @return
 */
ret_code_t hal_uart_set_evt_cb(hal_uart_t* h, hal_uart_evt_cb_t cb, void* user);

#endif  // HAL_UART_H
