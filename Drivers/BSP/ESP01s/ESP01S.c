#include "ESP01S.h"

#include <stdio.h>
#include <string.h>

#include "cmsis_os2.h"
#include "main.h"
#include "RingBuffer.h"
static char txBuffer[AT_CMD_BUFFER_SIZE];

osMutexId_t esp01s_Mutex01Handle;
ESP01S_Handle g_esp01_handle;
volatile uint8_t g_esp01s_flag;

/**
 * @brief 初始化 ESP01s，使用 UART3 DMA + 空闲中断
 */
void esp01s_Init(UART_HandleTypeDef *huart, uint16_t rb_size) {
    /* 1、初始化 ESP01s的句柄 */
    g_esp01_handle.rb.name = "ESP01s_handle.rb";
    g_esp01_handle.uart_p = huart;
    g_esp01_handle.rb_rx_size = rb_size;
    g_esp01_handle.rx_len = 0;

    /* 2、初始化句柄中的环形缓冲区 */
    if (!CreateRingBuffer(&g_esp01_handle.rb, g_esp01_handle.rb_rx_size)) {
        printf("esp01s_Init() 初始化缓冲区失败\n");
    } else {
        printf("esp01s_Init() 初始化缓冲区成功\n");
    }

    /* 3、启动空闲中断以及DMA 并关闭DMA半满中断防止干扰 */
    HAL_UARTEx_ReceiveToIdle_DMA(g_esp01_handle.uart_p,
                                 g_esp01_handle.rx_buffer,
                                 sizeof(g_esp01_handle.rx_buffer));
    __HAL_DMA_DISABLE_IT(g_esp01_handle.uart_p->hdmarx, DMA_IT_HT);

    /* 4、发送命令 */
    /* AT命令 测试连通性 */
    const char *txBuffer = convert_format("AT", NULL, 0, PF_QUOTE, true);
    if (command_send(g_esp01_handle.uart_p, txBuffer, "OK", 2000)) {
        printf("%s  响应成功\n", txBuffer);
    } else {
        printf("%s  响应失败\n", txBuffer);
    }

    // /* 开启station 模式 */
    // const char *txBuffer1 = convert_format("AT+CWMODE=", "1", 1, PF_NONE, true);
    // if (command_send(g_esp01_handle.uart_p, txBuffer1, "OK", 2000)) {
    //     printf("%s  响应成功\n", txBuffer1);
    // } else {
    //     printf("%s  响应失败\n", txBuffer1);
    // }
    //
    //
    // /* 开启SmartConfig */
    // const char *txBuffer2 = convert_format("AT+CWSTARTSMART", NULL, 0, PF_NONE, true);
    // if (command_send(g_esp01_handle.uart_p, txBuffer2, "OK", 3000)) {
    //     printf("%s  响应成功\n", txBuffer2);
    // } else {
    //     printf("%s  响应失败\n", txBuffer2);
    // }
    //
    // /* 关闭SmartConfig 模式 */
    // const char *txBuffer4 = convert_format("AT+CWSTOPSMART", NULL, 0, PF_NONE, true);
    // if (command_send(g_esp01_handle.uart_p, txBuffer4, "OK", 2000)) {
    //     printf("%s  响应成功\n", txBuffer4);
    // } else {
    //     printf("%s  响应失败\n", txBuffer4);
    // }
}

/**
 * @brief 检测发送的命令是否生效
 * @param huart 串口句柄
 * @param command 需要发送的命令
 * @param wait_rsu 需要等待回复的命令
 * @param max_wait_time 最大等待时间
 * @return 返回是否返回了指定的消息
 */
bool command_send(UART_HandleTypeDef *huart, const char *command, const char *wait_rsu, uint16_t max_wait_time) {
    /* 进入新的发送流程 初始化为0 */
    g_esp01s_flag = 0;
    memset(g_esp01_handle.rx_buffer, 0, g_esp01_handle.rx_len);
    g_esp01_handle.rx_len = 0;
    g_esp01_handle.rx_buffer[0] = '\0';


    __HAL_DMA_DISABLE_IT(huart->hdmarx, DMA_IT_HT);
    __HAL_DMA_DISABLE_IT(huart->hdmarx, DMA_IT_TC);

    /* 发送指定的命令 */
    HAL_UART_Transmit(huart, (const uint8_t *) command, strlen((char *) command), max_wait_time);

    /* 在最大等待时间内等待数据到来判断 是否包含所需要的 */
    while (max_wait_time--) {
        if (g_esp01s_flag) {
            if (strstr((char *) g_esp01_handle.rx_buffer, wait_rsu) != NULL) {
                return true;
            }
            g_esp01s_flag = 0;
        }
        osDelay(1);
    }
    osDelay(500);
    return false;
}

/**
 * @brief 将原始字符串加上额外的转义字符等
 * @param Command AT指令部分
 * @param param  参数部分
 * @param par_len 参数个数
 * @param pf 参数包裹符号选择
 * @param is_newline 是否结尾加上 \r\n
 * @return 新的转换好的字符串
 */
const char *convert_format(const char *Command, const char *param, const uint8_t par_len, Param_Format pf,
                           bool is_newline) {
    // 1. 初始化索引，清空缓冲区（可选）
    uint16_t idx = 0;

    // 2. 拼接 Command 部分
    if (Command != NULL) {
        while (*Command != '\0') {
            if (idx >= AT_CMD_BUFFER_SIZE - 1) {
                printf("字符串转译空间不够\n");
                return NULL;
            }
            txBuffer[idx++] = *Command++;
        }
    }

    // 3. 处理参数部分
    if (param != NULL && par_len > 0) {
        // 如果需要引号包裹，先加一个前置引号
        if (pf == PF_QUOTE) {
            if (idx < AT_CMD_BUFFER_SIZE - 1) txBuffer[idx++] = '\"';
        }

        // 遍历参数内容，进行转义处理
        for (uint8_t i = 0; i < par_len; i++) {
            // 检查缓冲区溢出
            if (idx >= AT_CMD_BUFFER_SIZE - 3) break; // 预留空间给结束符

            uint8_t ch = param[i];

            // --- 转义核心逻辑 ---
            // 如果模式是 PF_QUOTE，我们需要转义内部的特殊字符
            if (pf == PF_QUOTE) {
                if (ch == '\"' || ch == '\\') {
                    txBuffer[idx++] = '\\'; // 添加转义反斜杠
                }
            }

            txBuffer[idx++] = ch;
        }

        // 如果需要引号包裹，加一个后置引号
        if (pf == PF_QUOTE) {
            if (idx < AT_CMD_BUFFER_SIZE - 1) txBuffer[idx++] = '\"';
        }
    }

    // 4. 处理换行符 \r\n
    if (is_newline) {
        if (idx < AT_CMD_BUFFER_SIZE - 2) {
            txBuffer[idx++] = '\r';
            txBuffer[idx++] = '\n';
        }
    }

    // 5. 确保字符串以 NULL 结尾
    txBuffer[idx] = '\0';

    return txBuffer;
}
