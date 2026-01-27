//
// Created by Gemini on 2026/01/27.
//

#include "ota_storage.h"

#include "stm32f4xx_hal.h"


#define OTA_ERR_FLASH(reason) RET_MAKE_IO(RET_MOD_OTA, RET_SUB_OTA_IMAGE, reason)

/* =======================================================================
 * 辅助函数
 * ======================================================================= */

/**
 * @brief 根据地址获取 Flash 扇区编号
 */
static uint32_t GetSector(uint32_t Address) {
    uint32_t sector = 0;

    if ((Address < 0x08004000) && (Address >= 0x08000000)) {
        sector = FLASH_SECTOR_0;
    } else if ((Address < 0x08008000) && (Address >= 0x08004000)) {
        sector = FLASH_SECTOR_1;
    } else if ((Address < 0x0800C000) && (Address >= 0x08008000)) {
        sector = FLASH_SECTOR_2;
    } else if ((Address < 0x08010000) && (Address >= 0x0800C000)) {
        sector = FLASH_SECTOR_3;
    } else if ((Address < 0x08020000) && (Address >= 0x08010000)) {
        sector = FLASH_SECTOR_4;
    } else if ((Address < 0x08040000) && (Address >= 0x08020000)) {
        sector = FLASH_SECTOR_5;
    } else if ((Address < 0x08060000) && (Address >= 0x08040000)) {
        sector = FLASH_SECTOR_6;
    } else if ((Address < 0x08080000) && (Address >= 0x08060000)) {
        sector = FLASH_SECTOR_7;
    } else if ((Address < 0x080A0000) && (Address >= 0x08080000)) {
        sector = FLASH_SECTOR_8;
    } else if ((Address < 0x080C0000) && (Address >= 0x080A0000)) {
        sector = FLASH_SECTOR_9;
    } else if ((Address < 0x080E0000) && (Address >= 0x080C0000)) {
        sector = FLASH_SECTOR_10;
    } else if ((Address < 0x08100000) && (Address >= 0x080E0000)) {
        sector = FLASH_SECTOR_11;
    } else {
        // 错误或越界
        sector = FLASH_SECTOR_11;  // 默认安全值
    }
    return sector;
}

/* =======================================================================
 * 公共 API 实现
 * ======================================================================= */

ret_code_t ota_storage_init(void) {
    if (HAL_FLASH_Unlock() != HAL_OK) {
        return OTA_ERR_FLASH(RET_R_FLASH_ERR);
    }
    /* 清除挂起的标志位 */
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
    return RET_OK;
}

ret_code_t ota_storage_finalize(void) {
    if (HAL_FLASH_Lock() != HAL_OK) {
        return OTA_ERR_FLASH(RET_R_FLASH_ERR);
    }
    return RET_OK;
}

ret_code_t ota_storage_erase_region(uint32_t start_addr, uint32_t size) {
    uint32_t SectorError;
    FLASH_EraseInitTypeDef EraseInitStruct;

    /* 计算需要擦除的扇区范围 */
    const uint32_t FirstSector   = GetSector(start_addr);
    const uint32_t NbOfSectors   = GetSector(start_addr + size - 1) - FirstSector + 1;

    /* 填充擦除配置结构体 */
    EraseInitStruct.TypeErase    = FLASH_TYPEERASE_SECTORS;
    EraseInitStruct.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    EraseInitStruct.Sector       = FirstSector;
    EraseInitStruct.NbSectors    = NbOfSectors;

    /* 临界区: 禁用中断 */
    __disable_irq();

    if (HAL_FLASHEx_Erase(&EraseInitStruct, &SectorError) != HAL_OK) {
        __enable_irq();  // 恢复中断
        return OTA_ERR_FLASH(RET_R_FLASH_ERR);
    }

    __enable_irq();  // 恢复中断
    return RET_OK;
}

bool ota_storage_is_sector_start(uint32_t addr, uint32_t* out_sector_size) {
    /* STM32F4 Flash Sector Map */
    /* Sector 0-3: 16KB */
    /* Sector 4: 64KB */
    /* Sector 5-11: 128KB (0x20000) */

    // 我们主要关心 Slot 1 区域 (Sector 8, 9, 10...)
    // S8: 0x08080000 (128KB)
    // S9: 0x080A0000 (128KB)
    // S10: 0x080C0000 (128KB)
    // S11: 0x080E0000 (128KB)

    if (addr == 0x08080000 || addr == 0x080A0000 || addr == 0x080C0000 || addr == 0x080E0000) {
        if (out_sector_size) {
            *out_sector_size = 128 * 1024;
        }
        return true;
    }

    // 如果你还需要支持更前面的扇区，可以在这里加判断
    return false;
}

ret_code_t ota_storage_write(uint32_t addr, const uint8_t* data, uint32_t len) {
    uint32_t i = 0;

    /* 临界区 */
    __disable_irq();

    for (i = 0; i < len; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, addr + i, data[i]) != HAL_OK) {
            __enable_irq();
            return OTA_ERR_FLASH(RET_R_FLASH_ERR);
        }
    }

    __enable_irq();
    return RET_OK;
}
