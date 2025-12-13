#ifndef INC_ESP01S_H_
#define INC_ESP01S_H_
#include <stdbool.h>
#include <stdint.h>

#include "cmsis_os2.h"
#include "RingBuffer.h"
#include  "main.h"

/*　ESP01s句柄 */
typedef struct {
    UART_HandleTypeDef *uart_p; /* 需要使用的串口 */
    RingBuffer rb; /* 环形缓冲区的句柄 */
    volatile uint16_t rb_rx_size; /* 环形缓冲区初始化大小（字节）*/
    uint8_t rx_buffer[512]; /* 临时缓冲区 */
    volatile uint16_t rx_len; /* 临时缓冲区接收的数据长度 */
} ESP01S_Handle;

/* 当前参数的 包裹符号 */
typedef enum {
    PF_NONE = 0, // 不包裹
    PF_QUOTE, // 用 "param" 包裹
    PF_BRACKET, // 用 [param] 包裹
    PF_BRACE, // 用 {param} 包裹
} Param_Format;

#define AT_CMD_BUFFER_SIZE 128
extern osMutexId_t esp01s_Mutex01Handle;
extern ESP01S_Handle g_esp01_handle;
extern volatile uint8_t g_esp01s_flag;
bool command_send(UART_HandleTypeDef *huart, const char *command, const char *wait_rsu, uint16_t max_wait_time);

void esp01s_Init(UART_HandleTypeDef *huart, uint16_t rb_size);

const char *convert_format(const char *Command, const char *param, uint8_t par_len, Param_Format pf,
                           bool is_newline);
#endif /* INC_ESP01S_H_ */
