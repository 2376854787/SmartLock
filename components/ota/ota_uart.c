//
// Created by Gemini on 2026/01/27.
// 串口 OTA 下载模块实现
//

#include "ota_uart.h"

#include <string.h>

#include "log.h"
#include "ota_manager.h"
#include "usart.h"

#define OTA_UART_TAG "OTA_UART"

/* 帧解析状态机 */
typedef enum {
    STATE_WAIT_HEADER1,  // 等待 0x55
    STATE_WAIT_HEADER2,  // 等待 0xAA
    STATE_WAIT_CMD,      // 等待 CMD
    STATE_WAIT_LEN_H,    // 等待长度高字节
    STATE_WAIT_LEN_L,    // 等待长度低字节
    STATE_WAIT_DATA,     // 接收数据
    STATE_WAIT_CRC_H,    // 等待 CRC 高字节
    STATE_WAIT_CRC_L     // 等待 CRC 低字节
} ota_uart_state_t;

/* OTA 上下文 */
static struct {
    ota_uart_state_t state;
    uint8_t cmd;
    uint16_t data_len;
    uint16_t data_idx;
    uint8_t data_buf[1100];  // 最大 1024 + 冗余
    uint16_t crc_recv;

    bool ota_started;
    bool ota_complete;
    uint32_t image_size;
    uint32_t total_written;
} g_ctx;

/* 简单 CRC16-CCITT */
static uint16_t calc_crc16(const uint8_t* data, uint32_t len) {
    uint16_t crc = 0xFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/* 发送回复 */
static void send_reply(uint8_t reply) {
    HAL_UART_Transmit(&huart1, &reply, 1, 100);
}

/* 处理完整帧 */
static void process_frame(void) {
    /* 验证 CRC */
    uint16_t calc = calc_crc16(g_ctx.data_buf, g_ctx.data_len);
    if (calc != g_ctx.crc_recv) {
        LOG_E(OTA_UART_TAG, "CRC Error: recv=0x%04X calc=0x%04X", g_ctx.crc_recv, calc);
        send_reply(OTA_UART_NAK);
        return;
    }

    switch (g_ctx.cmd) {
        case OTA_UART_CMD_START: {
            if (g_ctx.data_len != 4) {
                LOG_E(OTA_UART_TAG, "START: Invalid len %d", g_ctx.data_len);
                send_reply(OTA_UART_NAK);
                return;
            }
            /* 解析镜像大小 (小端) */
            g_ctx.image_size = (uint32_t)g_ctx.data_buf[0] | ((uint32_t)g_ctx.data_buf[1] << 8) |
                               ((uint32_t)g_ctx.data_buf[2] << 16) |
                               ((uint32_t)g_ctx.data_buf[3] << 24);

            LOG_I(OTA_UART_TAG, "OTA Start, image_size=%lu", g_ctx.image_size);

            if (ota_manager_start(g_ctx.image_size) != RET_OK) {
                LOG_E(OTA_UART_TAG, "ota_manager_start failed");
                send_reply(OTA_UART_NAK);
                return;
            }

            g_ctx.ota_started   = true;
            g_ctx.ota_complete  = false;
            g_ctx.total_written = 0;
            send_reply(OTA_UART_ACK);
            break;
        }

        case OTA_UART_CMD_DATA: {
            if (!g_ctx.ota_started) {
                LOG_E(OTA_UART_TAG, "DATA: OTA not started");
                send_reply(OTA_UART_NAK);
                return;
            }

            if (ota_manager_write_chunk(g_ctx.total_written, g_ctx.data_buf, g_ctx.data_len) !=
                RET_OK) {
                LOG_E(OTA_UART_TAG, "Write chunk failed at offset %lu", g_ctx.total_written);
                send_reply(OTA_UART_NAK);
                return;
            }

            g_ctx.total_written += g_ctx.data_len;

            /* 每 64KB 打印进度 */
            if ((g_ctx.total_written & 0xFFFF) < g_ctx.data_len) {
                LOG_I(OTA_UART_TAG, "Progress: %lu / %lu (%d%%)", g_ctx.total_written,
                      g_ctx.image_size, (int)(g_ctx.total_written * 100 / g_ctx.image_size));
            }

            send_reply(OTA_UART_ACK);
            break;
        }

        case OTA_UART_CMD_END: {
            if (!g_ctx.ota_started) {
                LOG_E(OTA_UART_TAG, "END: OTA not started");
                send_reply(OTA_UART_NAK);
                return;
            }

            LOG_I(OTA_UART_TAG, "OTA End, total_written=%lu", g_ctx.total_written);

            if (ota_manager_finish() != RET_OK) {
                LOG_E(OTA_UART_TAG, "ota_manager_finish failed");
                send_reply(OTA_UART_NAK);
                return;
            }

            g_ctx.ota_complete = true;
            g_ctx.ota_started  = false;
            send_reply(OTA_UART_ACK);
            LOG_I(OTA_UART_TAG, "OTA Complete! Please reboot.");
            break;
        }

        case OTA_UART_CMD_ABORT: {
            LOG_W(OTA_UART_TAG, "OTA Aborted");
            ota_uart_reset();
            send_reply(OTA_UART_ACK);
            break;
        }

        default:
            LOG_E(OTA_UART_TAG, "Unknown CMD: 0x%02X", g_ctx.cmd);
            send_reply(OTA_UART_NAK);
            break;
    }
}

void ota_uart_process(const uint8_t* data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        uint8_t byte = data[i];

        switch (g_ctx.state) {
            case STATE_WAIT_HEADER1:
                if (byte == 0x55) {
                    g_ctx.state = STATE_WAIT_HEADER2;
                }
                break;

            case STATE_WAIT_HEADER2:
                if (byte == 0xAA) {
                    g_ctx.state = STATE_WAIT_CMD;
                } else {
                    g_ctx.state = STATE_WAIT_HEADER1;
                }
                break;

            case STATE_WAIT_CMD:
                g_ctx.cmd   = byte;
                g_ctx.state = STATE_WAIT_LEN_H;
                break;

            case STATE_WAIT_LEN_H:
                g_ctx.data_len = (uint16_t)byte << 8;
                g_ctx.state    = STATE_WAIT_LEN_L;
                break;

            case STATE_WAIT_LEN_L:
                g_ctx.data_len |= byte;
                g_ctx.data_idx = 0;
                if (g_ctx.data_len > 0 && g_ctx.data_len <= 1024) {
                    g_ctx.state = STATE_WAIT_DATA;
                } else if (g_ctx.data_len == 0) {
                    g_ctx.state = STATE_WAIT_CRC_H;
                } else {
                    LOG_E(OTA_UART_TAG, "Invalid data_len: %d", g_ctx.data_len);
                    g_ctx.state = STATE_WAIT_HEADER1;
                }
                break;

            case STATE_WAIT_DATA:
                g_ctx.data_buf[g_ctx.data_idx++] = byte;
                if (g_ctx.data_idx >= g_ctx.data_len) {
                    g_ctx.state = STATE_WAIT_CRC_H;
                }
                break;

            case STATE_WAIT_CRC_H:
                g_ctx.crc_recv = (uint16_t)byte << 8;
                g_ctx.state    = STATE_WAIT_CRC_L;
                break;

            case STATE_WAIT_CRC_L:
                g_ctx.crc_recv |= byte;
                process_frame();
                g_ctx.state = STATE_WAIT_HEADER1;
                break;
        }
    }
}

bool ota_uart_is_complete(void) {
    return g_ctx.ota_complete;
}

void ota_uart_reset(void) {
    memset(&g_ctx, 0, sizeof(g_ctx));
    g_ctx.state = STATE_WAIT_HEADER1;
}
