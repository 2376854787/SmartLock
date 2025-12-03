//
// Created by yan on 2025/11/7.
//

#ifndef SMARTCLOCK_MYPRINTF_H
#define SMARTCLOCK_MYPRINTF_H

#include <stdbool.h>
#include "main.h" // 包含 STM32 HAL 库和外设定义
#include "RingBuffer.h"

/**
 * @brief 初始化 DMA 日志记录器
 * @param huart 用于日志输出的 UART 句柄
 * @return 如果初始化成功返回 true, 否则返回 false
 */
bool dma_logger_init(UART_HandleTypeDef *huart);

/**
 * @brief 非阻塞的、DMA驱动的 printf 函数
 * @note  此函数是线程安全的
 * @param format 格式化字符串，用法同 printf
 * @param ...    可变参数
 */
void dma_printf(const char *format, ...);

#endif //SMARTCLOCK_MYPRINTF_H