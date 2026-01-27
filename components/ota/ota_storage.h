//
// Created by Gemini on 2026/01/27.
//

#ifndef SMARTCLOCK_OTA_STORAGE_H
#define SMARTCLOCK_OTA_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

#include "ret_code.h"

/* =======================================================================
 * STM32F407 Flash 布局配置
 * ======================================================================= */

/*
 * Primary Slot (Slot 0): 0x08020000 (Sector 5, 6, 7) - 大小 384KB
 * Secondary Slot (Slot 1): 0x08080000 (Sector 8, 9, 10) - 大小 384KB
 * Scratch area: 0x080E0000 (Sector 11) - 大小 128KB
 *
 * 我们将要写入的是 Slot 1 。
 */
#define OTA_STORAGE_SLOT1_START_ADDR 0x08080000
#define OTA_STORAGE_SLOT1_SIZE       (384 * 1024)

/* =======================================================================
 * 公共 API
 * ======================================================================= */

/**
 * @brief 初始化 OTA 存储 (解锁 Flash)
 * @return RET_OK 成功, 其它为错误码
 */
ret_code_t ota_storage_init(void);

/**
 * @brief 结束 OTA 存储操作 (上锁 Flash)
 * @return RET_OK 成功, 其它为错误码
 */
ret_code_t ota_storage_finalize(void);

/**
 * @brief 擦除 OTA 区域 (Slot 1)
 *
 * @note 这是一个阻塞操作，可能需要几秒钟。
 *       在关键擦除期间，中断将被禁用 (系统冻结)。
 *
 * @param start_addr 起始地址 (必须扇区对齐)
 * @param size 擦除大小
 * @return RET_OK 成功, 其它为错误码
 */
ret_code_t ota_storage_erase_region(uint32_t start_addr, uint32_t size);

/**
 * @brief 检查当前地址是否是某个 Flash 扇区的起始地址
 *
 * @param addr 绝对物理地址
 * @param out_sector_size 输出参数：如果是扇区起始，返回该扇区大小；否则不改变
 * @return true 是扇区起始, false 不是
 */
bool ota_storage_is_sector_start(uint32_t addr, uint32_t* out_sector_size);

/**
 * @brief 写入数据到 Flash
 *
 * @param addr 目标地址 (建议 4 字节对齐)
 * @param data 源数据指针
 * @param len 数据长度 (建议是 4 的倍数)
 * @return RET_OK 成功, 其它为错误码
 */
ret_code_t ota_storage_write(uint32_t addr, const uint8_t* data, uint32_t len);

#endif  // SMARTCLOCK_OTA_STORAGE_H
