#include "as608_port.h"

#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include "cmsis_os2.h"

/* LibDriver AS608 interface 声明（必须实现这些函数） */
#include "driver_as608_interface.h"

/* ============================ 配置区 ============================ */

#ifndef AS608_RX_RING_SIZE
#define AS608_RX_RING_SIZE 512u
#endif

#ifndef AS608_UART_TX_TIMEOUT_MS
#define AS608_UART_TX_TIMEOUT_MS 1000u
#endif

/* ============================ 环形缓冲区 ============================ */

static UART_HandleTypeDef *s_as608_huart = NULL;
static uint8_t s_rx_byte = 0;

static uint8_t s_rx_ring[AS608_RX_RING_SIZE];
static volatile uint16_t s_rx_head = 0; /* write */
static volatile uint16_t s_rx_tail = 0; /* read */

static inline uint16_t rb_next(uint16_t v)
{
    return (uint16_t)((v + 1u) % AS608_RX_RING_SIZE);
}

static inline uint16_t rb_count(void)
{
    uint16_t head = s_rx_head;
    uint16_t tail = s_rx_tail;
    if (head >= tail)
    {
        return (uint16_t)(head - tail);
    }
    else
    {
        return (uint16_t)(AS608_RX_RING_SIZE - (tail - head));
    }
}

static void rb_clear(void)
{
    __disable_irq();
    s_rx_head = 0;
    s_rx_tail = 0;
    __enable_irq();
}

static void rb_push(uint8_t b)
{
    uint16_t next = rb_next(s_rx_head);
    if (next == s_rx_tail)
    {
        /* 缓冲区满：丢弃最旧数据（tail++） */
        s_rx_tail = rb_next(s_rx_tail);
    }
    s_rx_ring[s_rx_head] = b;
    s_rx_head = next;
}

static uint16_t rb_pop(uint8_t *out, uint16_t want)
{
    uint16_t got = 0;

    __disable_irq();
    while ((got < want) && (s_rx_tail != s_rx_head))
    {
        out[got++] = s_rx_ring[s_rx_tail];
        s_rx_tail = rb_next(s_rx_tail);
    }
    __enable_irq();

    return got;
}

/* ============================ HAL 回调桥接 ============================ */

void AS608_Port_BindUart(UART_HandleTypeDef *huart)
{
    s_as608_huart = huart;
}

void AS608_Port_StartRx(void)
{
    if (s_as608_huart == NULL)
    {
        return;
    }

    rb_clear();
    /* 1 字节循环接收 */
    (void)HAL_UART_Receive_IT(s_as608_huart, &s_rx_byte, 1);
}

void AS608_Port_OnUartRxCplt(UART_HandleTypeDef *huart)
{
    if ((s_as608_huart != NULL) && (huart == s_as608_huart))
    {
        rb_push(s_rx_byte);
        /* 继续接收下一个字节 */
        (void)HAL_UART_Receive_IT(s_as608_huart, &s_rx_byte, 1);
    }
}

void AS608_Port_OnUartError(UART_HandleTypeDef *huart)
{
    if ((s_as608_huart != NULL) && (huart == s_as608_huart))
    {
        /* 常见错误：ORE(溢出)。处理方式：清标志并重启接收 */
        __HAL_UART_CLEAR_OREFLAG(huart);
        (void)HAL_UART_Receive_IT(s_as608_huart, &s_rx_byte, 1);
    }
}

void AS608_Port_FlushRx(void)
{
    rb_clear();
}

/* ============================ LibDriver interface 实现 ============================ */

uint8_t as608_interface_uart_init(void)
{
    /* UART 参数建议在 CubeMX 中配置好（57600 8N1）。这里负责启动 RX。 */
    AS608_Port_StartRx();
    return 0;
}

uint8_t as608_interface_uart_deinit(void)
{
    if (s_as608_huart == NULL)
    {
        return 0;
    }
    (void)HAL_UART_AbortReceive(s_as608_huart);
    rb_clear();
    return 0;
}

uint16_t as608_interface_uart_read(uint8_t *buf, uint16_t len)
{
    /*
     * driver_as608.c 的 read 语义：
     *  - 不是“阻塞读满 len”；而是“在 delay_ms 之后，把当前收齐的响应帧尽可能读出来”。
     *  - 该库在 STM32F407 示例里也会等待 1ms 观察 rx_point 是否还在增长。
     */

    if ((buf == NULL) || (len == 0u))
    {
        return 0;
    }

    /* 等待 RX 进入“静默”状态：1ms 内无新增字节（最多等待 5ms） */
    for (uint32_t i = 0; i < 5u; i++)
    {
        uint16_t c1 = rb_count();
        if (osKernelGetState() == osKernelRunning)
        {
            osDelay(1);
        }
        else
        {
            HAL_Delay(1);
        }
        uint16_t c2 = rb_count();
        if (c2 <= c1)
        {
            break;
        }
    }

    return rb_pop(buf, len);
}

uint8_t as608_interface_uart_write(uint8_t *buf, uint16_t len)
{
    if ((s_as608_huart == NULL) || (buf == NULL) || (len == 0u))
    {
        return 1;
    }

    /* 发送前清掉 RX 避免旧数据干扰（driver 本身也会 flush，但这里更保守） */
    /* 注意：如果你有上位机同时向模块发命令，这样 flush 会丢数据；建议 UART 独占。 */

    if (HAL_UART_Transmit(s_as608_huart, buf, len, AS608_UART_TX_TIMEOUT_MS) != HAL_OK)
    {
        return 1;
    }

    return 0;
}

uint8_t as608_interface_uart_flush(void)
{
    rb_clear();
    return 0;
}

void as608_interface_delay_ms(uint32_t ms)
{
    if (osKernelGetState() == osKernelRunning)
    {
        osDelay(ms);
    }
    else
    {
        HAL_Delay(ms);
    }
}

void as608_interface_debug_print(const char *const fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

