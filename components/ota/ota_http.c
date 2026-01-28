//
// Created by Gemini on 2026/01/27.
//

#include "ota_http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "AT.h"
#include "ESP01S.h"
#include "log.h"
#include "ota_manager.h"
#include "ota_storage.h"
#include "stm32f4xx_hal.h"

#define OTA_HTTP_TAG "OTA_HTTP"

extern AT_Manager_t g_at_manager;

/*
 * 极简 HTTP 解析状态机
 */
typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_WAIT_IPD,     // 等待 +IPD 头
    OTA_STATE_READ_PAYLOAD  // 读取剩余 Payload
} ota_rx_state_t;

static struct {
    ota_rx_state_t state;
    uint32_t total_written;
    uint32_t payload_bytes_left;  // 当前 IPD 包剩余需读取的字节数
    bool http_header_found;       // 是否已经跳过了 HTTP 头
    bool connection_closed;       // TCP 连接已关闭标志
    uint32_t content_length;      // 从 HTTP 头解析出的总长度 (可选校验)
    /* IPD 头缓存：处理 +IPD,xxx: 被帧边界切分的情况 */
    char ipd_header_buf[20];  // 存储被切分的 "+IPD,xxxx"
    uint8_t ipd_header_len;   // 当前缓存长度
    /* HTTP 头累积：缓存整个 HTTP 头以支持跨包解析 */
    char http_header_buf[512];  // 512字节通常足够容纳标准响应头
    uint16_t http_header_len;   // 当前已缓存长度
} g_ota_ctx;

/*
 * 核心 Hook 函数：拦截 AT 接收数据
 * 注意：此函数在 AT Core 任务上下文中执行
 */
/*
 * 核心 Hook 函数：拦截 AT 接收数据
 * 改进版：健壮处理分包
 */
static bool ota_http_raw_hook(AT_Manager_t* mgr, uint8_t* data, uint16_t len) {
    (void)mgr;
    static uint32_t hook_call_count = 0;

    if (g_ota_ctx.state == OTA_STATE_IDLE) {
        return false;  // 不拦截
    }

    /* 每 50 次调用打印一次诊断 */
    hook_call_count++;
    if (hook_call_count % 50 == 1) {
        LOG_I(OTA_HTTP_TAG, "Hook#%lu len=%u state=%d", hook_call_count, len, g_ota_ctx.state);
    }

    /*
     * 注意：CLOSED 检测已移到 WAIT_IPD 状态内部
     * 因为在 READ_PAYLOAD 状态下，"CLOSED" 可能是二进制数据的一部分
     */

    uint16_t processed = 0;
    while (processed < len) {
        uint8_t* curr_ptr = data + processed;
        uint16_t curr_len = len - processed;

        if (g_ota_ctx.state == OTA_STATE_WAIT_IPD) {
            /* 检测 CLOSED 消息 - 只在等待 IPD 时检测，避免二进制数据误判 */
            if (curr_len >= 6 && memcmp(curr_ptr, "CLOSED", 6) == 0) {
                LOG_W(OTA_HTTP_TAG, "Connection CLOSED detected!");
                g_ota_ctx.connection_closed = true;
                return true;
            }

            /* 策略：处理 +IPD,xxx: 头被帧边界切分的情况 */

            /* 1. 如果缓存中有被切分的 IPD 头残留，尝试拼接解析 */
            if (g_ota_ctx.ipd_header_len > 0) {
                /* 尝试在当前帧中找冒号 */
                char* colon = memchr(curr_ptr, ':', (curr_len < 10) ? curr_len : 10);
                if (colon) {
                    /* 拼接：缓存 + 当前帧到冒号的部分 */
                    int append_len = (colon - (char*)curr_ptr) + 1;
                    if (g_ota_ctx.ipd_header_len + append_len < sizeof(g_ota_ctx.ipd_header_buf)) {
                        memcpy(g_ota_ctx.ipd_header_buf + g_ota_ctx.ipd_header_len, curr_ptr,
                               append_len);
                        g_ota_ctx.ipd_header_buf[g_ota_ctx.ipd_header_len + append_len] = '\0';

                        /* 解析完整的 +IPD,xxx: 字符串 */
                        char* comma = strchr(g_ota_ctx.ipd_header_buf, ',');
                        if (comma) {
                            int payload_len = 0;
                            for (char* p = comma + 1; *p && *p != ':'; p++) {
                                if (*p >= '0' && *p <= '9') {
                                    payload_len = payload_len * 10 + (*p - '0');
                                }
                            }
                            if (payload_len > 0) {
                                g_ota_ctx.payload_bytes_left = payload_len;
                                g_ota_ctx.state              = OTA_STATE_READ_PAYLOAD;
                                g_ota_ctx.ipd_header_len     = 0; /* 清空缓存 */
                                processed += append_len;
                                // LOG_D: "IPD joined: len=%d"
                                continue;
                            }
                        }
                    }
                }
                /* 拼接失败，清空缓存，回退到正常扫描 */
                g_ota_ctx.ipd_header_len = 0;
            }

            /* 2. 正常扫描 +IPD */
            char* ipd_str  = NULL;
            int ipd_offset = -1;
            for (int i = 0; i + 3 < curr_len; i++) {
                if (curr_ptr[i] == '+' && curr_ptr[i + 1] == 'I' && curr_ptr[i + 2] == 'P' &&
                    curr_ptr[i + 3] == 'D') {
                    ipd_str    = (char*)&curr_ptr[i];
                    ipd_offset = i;
                    break;
                }
            }

            if (ipd_str) {
                /* 找冒号 */
                char* colon   = strchr(ipd_str, ':');
                int remaining = curr_len - ipd_offset;

                if (colon && (colon - ipd_str) < remaining) {
                    /* 冒号在当前帧中，直接解析 */
                    char* comma = strchr(ipd_str, ',');
                    if (comma && comma < colon) {
                        int payload_len = 0;
                        for (char* p = comma + 1; p < colon; p++) {
                            if (*p >= '0' && *p <= '9') {
                                payload_len = payload_len * 10 + (*p - '0');
                            }
                        }
                        if (payload_len > 0) {
                            g_ota_ctx.payload_bytes_left = payload_len;
                            g_ota_ctx.state              = OTA_STATE_READ_PAYLOAD;
                            processed += (colon - (char*)curr_ptr) + 1;
                            // LOG_D: "IPD: len=%d"
                            continue;
                        }
                    }
                } else {
                    /* 冒号不在当前帧，缓存 +IPD 开头到帧末尾的数据 */
                    int save_len = curr_len - ipd_offset;
                    if (save_len > 0 && save_len < (int)sizeof(g_ota_ctx.ipd_header_buf)) {
                        memcpy(g_ota_ctx.ipd_header_buf, ipd_str, save_len);
                        g_ota_ctx.ipd_header_len = save_len;
                        // LOG_D: "IPD split, cached"
                    }
                    return true;
                }
            }

            /* 没找到 +IPD */
            return true;

        } else if (g_ota_ctx.state == OTA_STATE_READ_PAYLOAD) {
            /* 直接读取数据 */
            uint16_t chunk_len =
                (curr_len < g_ota_ctx.payload_bytes_left) ? curr_len : g_ota_ctx.payload_bytes_left;

            uint8_t* write_ptr = curr_ptr;
            uint16_t write_len = chunk_len;

            /* HTTP 头过滤 */
            if (!g_ota_ctx.http_header_found) {
                /* 将当前数据追加到头部缓冲区 */
                uint16_t space = sizeof(g_ota_ctx.http_header_buf) - g_ota_ctx.http_header_len - 1;
                uint16_t copy_len = (chunk_len < space) ? chunk_len : space;

                memcpy(g_ota_ctx.http_header_buf + g_ota_ctx.http_header_len, write_ptr, copy_len);
                g_ota_ctx.http_header_len += copy_len;
                g_ota_ctx.http_header_buf[g_ota_ctx.http_header_len] = '\0';  // 保持 null 结尾

                /* 搜索 \r\n\r\n */
                char* header_end = strstr(g_ota_ctx.http_header_buf, "\r\n\r\n");

                if (header_end) {
                    g_ota_ctx.http_header_found = true;
                    LOG_I(OTA_HTTP_TAG, "HTTP Header Found!");

                    /* 解析 Content-Length (现在我们可以安全地搜索整个缓冲区了) */
                    char* cl_ptr = strstr(g_ota_ctx.http_header_buf, "Content-Length:");
                    if (!cl_ptr) {
                        cl_ptr = strstr(g_ota_ctx.http_header_buf, "content-length:");
                    }
                    /* TODO: 可以添加更多变体，或者实现 strcasestr */

                    if (cl_ptr) {
                        cl_ptr += 15;
                        while (*cl_ptr == ' ') cl_ptr++;
                        g_ota_ctx.content_length = (uint32_t)atoi(cl_ptr);
                        LOG_I(OTA_HTTP_TAG, "Content-Length: %lu", g_ota_ctx.content_length);
                    } else {
                        LOG_W(OTA_HTTP_TAG, "Content-Length not found in header buffer");
                        // DEBUG: 打印部分头信息
                        g_ota_ctx.http_header_buf[128] = '\0';
                        LOG_W(OTA_HTTP_TAG, "Header start: %s", g_ota_ctx.http_header_buf);
                    }

                    /* 计算当前帧中属于 Body 的数据 */
                    /* header_end 指向 \r\n\r\n 的开头 */
                    /* 我们需要找出 header_end 在当前帧中的对应位置?
                       不，我们只需要计算 Body 开始的绝对偏移，然后减去之前的 header_len */

                    /* 计算头部总长度 (包含 \r\n\r\n 4字节) */
                    size_t total_header_size = (header_end - g_ota_ctx.http_header_buf) + 4;

                    /* Body 在当前帧中的起始位置 = 总头部长度 - 这一帧之前已累积的长度 */
                    size_t prev_len          = g_ota_ctx.http_header_len - copy_len;

                    if (total_header_size >= prev_len) {
                        size_t offset_in_chunk = total_header_size - prev_len;
                        if (offset_in_chunk < chunk_len) {
                            write_ptr += offset_in_chunk;
                            write_len -= offset_in_chunk;
                        } else {
                            write_len = 0;  // Body 还没开始
                        }
                    } else {
                        // 异常情况：Body 应该在之前就始了？
                        // 由于我们之前 write_len=0，数据被丢弃了。
                        // 如果 header_end 出现在之前的帧里，我们会在当时就发现。
                        // 所以这里只会是 offset_in_chunk >= 0
                        write_len = 0;
                    }
                } else {
                    /* 头没结束，丢弃当前所有数据 (已存入 buffer) */
                    write_len = 0;

                    /* 溢出保护 */
                    if (space == 0) {
                        LOG_E(OTA_HTTP_TAG, "HTTP Header buffer overflow! Resetting.");
                        g_ota_ctx.http_header_len = 0;  // 暴力重置，可能会失败，但在嵌入式这是兜底
                    }
                }
            }

            /* 写入 Flash */
            if (write_len > 0) {
                ota_manager_write_chunk(g_ota_ctx.total_written, write_ptr, write_len);
                g_ota_ctx.total_written += write_len;
                if (g_ota_ctx.total_written % 10240 < write_len) {
                    LOG_I(OTA_HTTP_TAG, "Download: %lu bytes", g_ota_ctx.total_written);
                }
            }

            g_ota_ctx.payload_bytes_left -= chunk_len;
            processed += chunk_len;

            if (g_ota_ctx.payload_bytes_left == 0) {
                g_ota_ctx.state = OTA_STATE_WAIT_IPD;
            }
        }
    }

    return true;  // 拦截所有数据
}

/*
 * 辅助：发送 HTTP GET 请求
 */
static ret_code_t send_http_request(const ota_http_config_t* config) {
    char cmd_buffer[256];

    // 0. 先关闭可能存在的旧连接
    AT_SendCmd(&g_at_manager, "AT+CIPCLOSE\r\n", "OK", 2000);  // 忽略结果
    osDelay(500);

    // 1. 建立 TCP 连接 (重试最多 3 次)
    snprintf(cmd_buffer, sizeof(cmd_buffer), "AT+CIPSTART=\"TCP\",\"%s\",%d\r\n", config->server_ip,
             config->server_port);

    int retry      = 0;
    bool connected = false;
    while (retry < 3 && !connected) {
        /*
         * ESP01S AT+CIPSTART 成功响应格式：
         *   CONNECT
         *   OK
         * 失败响应：
         *   ERROR
         *   ALREADY CONNECTED
         *
         * 我们匹配 "OK" 因为成功时 CONNECT 后总会跟着 OK
         * 超时设置为 10 秒
         */
        LOG_I(OTA_HTTP_TAG, "Connecting to %s:%d...", config->server_ip, config->server_port);
        AT_Resp_t resp = AT_SendCmd(&g_at_manager, cmd_buffer, "OK", 20000);
        if (resp == AT_RESP_OK) {
            connected = true;
            LOG_I(OTA_HTTP_TAG, "TCP Connected!");
        } else {
            retry++;
            LOG_W(OTA_HTTP_TAG, "TCP Connect retry %d/3 (resp=%d)", retry, resp);
            /* 关闭可能的半开连接 */
            AT_SendCmd(&g_at_manager, "AT+CIPCLOSE\r\n", "OK", 2000);
            osDelay(1000);
        }
    }

    if (!connected) {
        LOG_E(OTA_HTTP_TAG, "TCP Connect Failed after 3 retries");
        return RET_R_IO;
    }

    // 2. 准备 GET 请求
    char http_req[512];
    const int len = snprintf(http_req, sizeof(http_req),
                             "GET %s HTTP/1.1\r\n"
                             "Host: %s:%d\r\n"
                             "Connection: close\r\n"
                             "\r\n",
                             config->url_path, config->server_ip, config->server_port);

    // 3. 发送长度
    snprintf(cmd_buffer, sizeof(cmd_buffer), "AT+CIPSEND=%d\r\n", len);
    if (AT_SendCmd(&g_at_manager, cmd_buffer, ">", 2000) != AT_RESP_OK) {
        LOG_E(OTA_HTTP_TAG, "CIPSEND Failed");
        return RET_R_IO;
    }

    // 4. 发送内容 (使用透传或者直接发字节)
    // 注意：AT_SendCmd 内部可能不支持发纯二进制流，但 HTTP 请求头是 Ascii，所以 OK。
    // 这里我们直接调用底层的 HAL 发送，绕过 AT parser 对 ">" 的等待（反正已经等过了）
    if (g_esp01_handle.uart_p) {
        HAL_UART_Transmit(g_esp01_handle.uart_p, (uint8_t*)http_req, len, 1000);
    }

    LOG_I(OTA_HTTP_TAG, "HTTP Request Sent");
    return RET_OK;
}

/*
 * 核心：OTA 下载流程
 */
ret_code_t ota_http_download(const ota_http_config_t* config) {
    ret_code_t ret;

    /* 1. 复位上下文 */
    memset(&g_ota_ctx, 0, sizeof(g_ota_ctx));
    g_ota_ctx.state = OTA_STATE_IDLE;

    /* 2. 预备 OTA Manager */
    if ((ret = ota_manager_start(OTA_STORAGE_SLOT1_SIZE)) != RET_OK) {
        LOG_E(OTA_HTTP_TAG, "OTA Start Failed: %d", ret);
        return ret;
    }

    /* 3. 先发送 HTTP 请求（包括建立 TCP 连接），然后再设置 Hook
     *    这样 TCP 连接的 "OK" 响应不会被 Hook 拦截
     */
    if ((ret = send_http_request(config)) != RET_OK) {
        return ret;
    }

    /* 4. 注册 Hook（在 HTTP 请求发送成功后）
     *    此时 ESP01S 开始接收服务器数据，需要拦截 +IPD
     */
    g_ota_ctx.state = OTA_STATE_WAIT_IPD;
    AT_SetRawDataHook(&g_at_manager, ota_http_raw_hook);
    LOG_I(OTA_HTTP_TAG, "Hook registered, waiting for data...");

    /* 5. 等待下载完成 */
    /* 由于 Hook 在后台运行，主线程需要等待
       等待条件：
       A. 收到 "CLOSED" (连接断开)
       B. 超时
       C. 固件写入量达到预期 (如果知道 Content-Length)
    */
    LOG_I(OTA_HTTP_TAG, "Downloading...");

    uint32_t timeout_counter = 0;
    uint32_t last_written    = 0;

    while (timeout_counter < 600) {  // 最多等 60 秒 (600 * 100ms)
        osDelay(100);

        // 检查进度
        if (g_ota_ctx.total_written > last_written) {
            last_written    = g_ota_ctx.total_written;
            timeout_counter = 0;  // 喂狗：只要有数据就不超时
        } else {
            timeout_counter++;
        }

        /* 每 2 秒打印一次诊断信息 */
        if (timeout_counter > 0 && timeout_counter % 20 == 0) {
            LOG_W(OTA_HTTP_TAG, "OTA status: written=%lu state=%d left=%lu",
                  g_ota_ctx.total_written, g_ota_ctx.state, g_ota_ctx.payload_bytes_left);
        }

        /* 智能提前结束：已下载超过 100KB，且连续 10 秒无新数据，认为下载完成 */
        if (g_ota_ctx.total_written > 100 * 1024 && timeout_counter >= 100) {
            LOG_W(OTA_HTTP_TAG, "Early finish: %lu bytes, no data for 10s",
                  g_ota_ctx.total_written);
            break;
        }

        /* 检测到 CLOSED - 连接已关闭，立即结束 */
        if (g_ota_ctx.connection_closed) {
            LOG_W(OTA_HTTP_TAG, "Connection closed, finishing download. Total: %lu bytes",
                  g_ota_ctx.total_written);
            break;
        }

        /* Content-Length 完成检测：已达到预期下载量，立即结束 */
        if (g_ota_ctx.content_length > 0 && g_ota_ctx.total_written >= g_ota_ctx.content_length) {
            LOG_I(OTA_HTTP_TAG, "Download complete! Received: %lu / %lu bytes",
                  g_ota_ctx.total_written, g_ota_ctx.content_length);
            break;
        }
    }

    /* 6. 结束 */
    AT_SetRawDataHook(&g_at_manager, NULL);

    LOG_I(OTA_HTTP_TAG, "Download finished. Total: %d bytes", g_ota_ctx.total_written);

    if (g_ota_ctx.total_written > 0) {
        ota_manager_finish();
        return RET_OK;
    } else {
        return RET_R_TIMEOUT;
    }
}
