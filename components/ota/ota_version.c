//
// Created by Gemini on 2026/01/27.
// MCUBoot 镜像头版本读取实现
//

#include "ota_version.h"

#include <stdio.h>
#include <string.h>

/*
 * MCUBoot 镜像头结构 (简化版)
 * 参考: https://docs.mcuboot.com/design.html#image-format
 *
 * Offset  Size  Field
 * 0x00    4     ih_magic (0x96f3b83d)
 * 0x04    4     ih_load_addr
 * 0x08    2     ih_hdr_size
 * 0x0A    2     ih_protect_tlv_size
 * 0x0C    4     ih_img_size
 * 0x10    4     ih_flags
 * 0x14    8     ih_ver (version)
 * 0x1C    4     _pad1
 */

#define MCUBOOT_IMAGE_MAGIC   0x96f3b83d
#define MCUBOOT_HEADER_OFFSET 0x200 /* imgtool --header-size 0x200 */

/* 应用起始地址 (Slot 0) - 需要根据你的 linker script 调整 */
#define APP_START_ADDRESS 0x08020000

/* 镜像头实际位置 = APP_START - HEADER_SIZE (因为 header 在 app 代码前面) */
/* 但如果使用 --pad-header，header 就在 APP_START_ADDRESS 开始 */
#define IMAGE_HEADER_ADDRESS (APP_START_ADDRESS)

int ota_get_running_version(mcuboot_version_t* ver) {
    if (ver == NULL) {
        return -1;
    }

    /* 读取镜像头 */
    const uint32_t* header = (const uint32_t*)IMAGE_HEADER_ADDRESS;

    /* 检查 Magic */
    if (header[0] != MCUBOOT_IMAGE_MAGIC) {
        /* 如果没有有效的 MCUBoot 头，返回默认版本 */
        ver->major     = 0;
        ver->minor     = 0;
        ver->revision  = 0;
        ver->build_num = 0;
        return -1;
    }

    /* 读取版本 (偏移 0x14) */
    const uint8_t* ver_ptr = (const uint8_t*)(IMAGE_HEADER_ADDRESS + 0x14);
    ver->major             = ver_ptr[0];
    ver->minor             = ver_ptr[1];
    ver->revision          = (uint16_t)ver_ptr[2] | ((uint16_t)ver_ptr[3] << 8);
    ver->build_num         = (uint32_t)ver_ptr[4] | ((uint32_t)ver_ptr[5] << 8) |
                     ((uint32_t)ver_ptr[6] << 16) | ((uint32_t)ver_ptr[7] << 24);

    return 0;
}

char* ota_version_to_string(const mcuboot_version_t* ver, char* buf, uint32_t buf_len) {
    if (ver == NULL || buf == NULL || buf_len < 16) {
        return NULL;
    }

    if (ver->build_num > 0) {
        snprintf(buf, buf_len, "v%d.%d.%d+%lu", ver->major, ver->minor, ver->revision,
                 (unsigned long)ver->build_num);
    } else {
        snprintf(buf, buf_len, "v%d.%d.%d", ver->major, ver->minor, ver->revision);
    }

    return buf;
}
