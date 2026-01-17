#ifndef HAL_UART_H
#define HAL_UART_H
#include <stdint.h>

#include "ret_code.h"

/* 串口号定义 */
typedef enum {
    HAL_UART_ID_0 = 0,
    HAL_UART_ID_1,
    HAL_UART_ID_2,
    HAL_UART_ID_3,
    HAL_UART_ID_6,
    HAL_UART_ID_7,
    HAL_UART_ID_8,
    HAL_UART_ID_9,
    HAL_UART_ID_MAX  // 大小会影响后面实现静态串口数组的大小
} hal_uart_id_t;

/* 句柄定义：应用层只持有指针 */
typedef void *hal_uart_handle_t;

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

/* 串口事件回调 */
typedef void (*hal_uart_evt_cb_t)(void *user, const hal_uart_event_t *evt);

typedef struct {
    uint32_t baud;                /*　波特率　*/
    uart_word_length_t data_bits; /* 数据位 */
    uart_Stop_Bits stop_bits;     /* 停止位 */
    uint8_t parity;               /* 优先级 */
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
typedef void (*hal_uart_rx_cb_t)(void *user_ctx, uint8_t *data, uint16_t len);


/**
 * @brief 启动异步接收（非阻塞）
 * @param handle 句柄
 * @param rx_buffer 接收缓冲区（通常由上层提供）
 * @param max_len   缓冲区最大长度
 * @note 底层通常开启 DMA + IDLE 中断。只要收到一串连续数据且总线空闲，就触发 RX_DATA 回调。
 */
ret_code_t hal_uart_read_async(hal_uart_handle_t handle, uint8_t *rx_buffer, uint16_t max_len);



ret_code_t hal_uart_open(hal_uart_id_t id, const hal_uart_cfg_t *cfg, hal_uart_t **out);
ret_code_t hal_uart_close(hal_uart_t *h);

ret_code_t hal_uart_rx_start(hal_uart_t *h);

ret_code_t hal_uart_read(hal_uart_t *h, uint8_t *out, uint32_t max, uint32_t *nread);

ret_code_t hal_uart_set_evt_cb(hal_uart_t *h, hal_uart_evt_cb_t cb, void *user);

/* 临界区保护：商业级架构必备，用于解决并发冲突 */
void hal_enter_critical(void);

void hal_exit_critical(void);
#endif  // HAL_UART_H
