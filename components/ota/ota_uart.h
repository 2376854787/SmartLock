//
// Created by Gemini on 2026/01/27.
// 串口 OTA 下载模块 - 通过串口接收固件直接写入 Flash
//

#ifndef SMARTCLOCK_OTA_UART_H
#define SMARTCLOCK_OTA_UART_H

#include <stdint.h>

#include "ret_code.h"

/**
 * @brief 串口 OTA 协议状态机
 *
 * 协议格式 (基于帧的二进制协议):
 * [0x55] [0xAA] [CMD] [LEN_H] [LEN_L] [DATA...] [CRC_H] [CRC_L]
 *
 * CMD:
 *   0x01 = OTA_START  : 开始 OTA，DATA = [SIZE_4B] 镜像大小 (小端)
 *   0x02 = OTA_DATA   : 数据帧，DATA = 最多 1024 字节的固件数据
 *   0x03 = OTA_END    : 结束 OTA，写入 Magic
 *   0x04 = OTA_ABORT  : 取消 OTA
 *
 * 回复 (单字节):
 *   0x06 = ACK   成功
 *   0x15 = NAK   失败
 */

#define OTA_UART_CMD_START 0x01
#define OTA_UART_CMD_DATA  0x02
#define OTA_UART_CMD_END   0x03
#define OTA_UART_CMD_ABORT 0x04

#define OTA_UART_ACK 0x06
#define OTA_UART_NAK 0x15

/**
 * @brief 处理串口接收到的数据 (应在 UART 任务中调用)
 *
 * @param data 接收到的原始数据
 * @param len 数据长度
 */
void ota_uart_process(const uint8_t* data, uint32_t len);

/**
 * @brief 检查 OTA 是否完成
 *
 * @return true 如果 OTA 完成，需要重启
 */
bool ota_uart_is_complete(void);

/**
 * @brief 复位 OTA 状态机
 */
void ota_uart_reset(void);

#endif  // SMARTCLOCK_OTA_UART_H
