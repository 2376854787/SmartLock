//
// Created by Gemini on 2026/01/27.
// MCUBoot 镜像头版本读取
//

#ifndef SMARTCLOCK_OTA_VERSION_H
#define SMARTCLOCK_OTA_VERSION_H

#include <stdint.h>

/**
 * @brief MCUBoot 镜像版本结构 (来自 imgtool)
 */
typedef struct {
    uint8_t major;
    uint8_t minor;
    uint16_t revision;
    uint32_t build_num;
} mcuboot_version_t;

/**
 * @brief 从当前运行的镜像头读取版本号
 *
 * @param ver 输出版本信息
 * @return 0 成功, -1 失败 (无效的 Magic)
 */
int ota_get_running_version(mcuboot_version_t* ver);

/**
 * @brief 格式化版本字符串
 *
 * @param ver 版本信息
 * @param buf 输出缓冲区
 * @param buf_len 缓冲区长度
 * @return 格式化后的字符串指针 (即 buf)
 */
char* ota_version_to_string(const mcuboot_version_t* ver, char* buf, uint32_t buf_len);

#endif  // SMARTCLOCK_OTA_VERSION_H
