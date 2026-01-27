//
// Created by Gemini on 2026/01/27.
//

#ifndef SMARTCLOCK_OTA_HTTP_H
#define SMARTCLOCK_OTA_HTTP_H

#include <stdint.h>

#include "ret_code.h"


/* =======================================================================
 * OTA HTTP 下载器 (基于 ESP01s AT 指令)
 * ======================================================================= */

/**
 * @brief 网络 OTA 配置结构体
 */
typedef struct {
    const char* server_ip;  // 服务器 IP (e.g. "192.168.1.100")
    uint16_t server_port;   // 服务器端口 (e.g. 8080)
    const char* url_path;   // 固件路径 (e.g. "/firmware.bin")
} ota_http_config_t;

/**
 * @brief 启动 HTTP 下载流程 (阻塞或异步任务驱动)
 *
 * @note 此函数将尝试:
 *       1. 初始化 ota_manager (不擦除)
 *       2. 连接服务器 (AT+CIPSTART)
 *       3. 发送 HTTP GET
 *       4. 接收并解析 HTTP Body
 *       5. 写 Flash (自动 Lazy Erase)
 *       6. 校验并 finish
 *
 * @param config 下载配置
 * @return RET_OK 成功
 */
ret_code_t ota_http_download(const ota_http_config_t* config);

#endif  // SMARTCLOCK_OTA_HTTP_H
