//
// Created by Gemini on 2026/01/27.
//

#include "ota_manager.h"

#include <string.h>

#include "ota_storage.h"
#include "log.h"

/* =======================================================================
 * 常量 & 宏
 * ======================================================================= */

/* MCUboot Magic (16 字节)
   0x77, 0xc2, 0x95, 0xf3, 0x60, 0xd2, 0xef, 0x7f,
   0x35, 0x58, 0x50, 0x72, 0xb4, 0x5c, 0x37, 0xc0
*/
static const uint8_t MCUBoot_Magic[] = {0x77, 0xc2, 0x95, 0xf3, 0x60, 0xd2, 0xef, 0x7f,
                                        0x35, 0x52, 0x50, 0x0f, 0x2c, 0xb6, 0x79, 0x80};

#define OTA_ERR_PARAM(reason) RET_MAKE_PARAM(RET_MOD_OTA, RET_SUB_OTA_IMAGE, reason)
#define OTA_ERR_DATA(reason)  RET_MAKE_DATA(RET_MOD_OTA, RET_SUB_OTA_IMAGE, reason)

/* =======================================================================
 * 实现
 * ======================================================================= */

ret_code_t ota_manager_start(uint32_t image_size) {
    if (image_size > OTA_STORAGE_SLOT1_SIZE) {
        return OTA_ERR_DATA(RET_R_DATA_OVERFLOW);  // 镜像过大
    }

    ret_code_t ret = ota_storage_init();
    if (ret != RET_OK) {
        return ret;
    }

    /* 优化策略: 恢复 Start 时全擦除，避免下载过程中擦除导致 UART 阻塞丢包 */
    ret = ota_storage_erase_region(OTA_STORAGE_SLOT1_START_ADDR, OTA_STORAGE_SLOT1_SIZE);
    if (ret != RET_OK) {
        LOG_E("OTA", "Failed to erase Slot 1: %d", ret);
        return ret;
    }
    LOG_I("OTA", "Slot 1 erased successfully (384KB)");

    return RET_OK;
}

ret_code_t ota_manager_write_chunk(uint32_t offset, const uint8_t* data, uint32_t len) {
    /* 校验边界 */
    if (offset + len > OTA_STORAGE_SLOT1_SIZE) {
        return OTA_ERR_PARAM(RET_R_RANGE_ERR);
    }

    uint32_t current_addr_start = OTA_STORAGE_SLOT1_START_ADDR + offset;

    /* 1. 检查是否跨越了扇区边界 (Lazy Erase) -> 已移除，改为 Upfront Erase */

    /* 2. 写入 Flash */
    return ota_storage_write(current_addr_start, data, len);
}

#include "log.h"  // Ensure log is included
ret_code_t ota_manager_finish(void) {
    /* ... variable defs ... */
    const uint32_t trailer_magic_addr = OTA_STORAGE_SLOT1_START_ADDR + OTA_STORAGE_SLOT1_SIZE - 16;
    const uint32_t trailer_swap_info_addr =
        OTA_STORAGE_SLOT1_START_ADDR + OTA_STORAGE_SLOT1_SIZE - 40;

    /* Debug: Read before write */
    uint8_t current_magic[sizeof(MCUBoot_Magic)];
    memcpy(current_magic, (void*)trailer_magic_addr, sizeof(MCUBoot_Magic));
    LOG_I("OTA_DEBUG", "Current Flash Content at Magic: %02X %02X ... %02X", current_magic[0],
          current_magic[1], current_magic[15]);

    // Check if already correct
    if (memcmp(current_magic, MCUBoot_Magic, sizeof(MCUBoot_Magic)) == 0) {
        LOG_I("OTA_DEBUG", "Magic already present.");
    } else {
        // Check if erased
        bool is_erased = true;
        for (int i = 0; i < sizeof(MCUBoot_Magic); i++) {
            if (current_magic[i] != 0xFF) is_erased = false;
        }
        if (!is_erased) {
            LOG_W("OTA_DEBUG", "Warning: Flash not erased at trailer! Write may fail.");
        }
    }

    /* 1. 解锁 Flash */
    ret_code_t ret = ota_storage_init();
    if (ret != RET_OK) {
        LOG_E("OTA_DEBUG", "Storage Init Failed: %d", ret);
        return ret;
    }

    /* 2. 写入 Magic */
    ret = ota_storage_write(trailer_magic_addr, MCUBoot_Magic, sizeof(MCUBoot_Magic));
    if (ret != RET_OK) {
        LOG_E("OTA_DEBUG", "Write Magic Failed: %d", ret);
        return ret;
    }

    /* 3. 写入 Swap Type (BOOT_SWAP_TYPE_TEST = 2) */
    uint8_t swap_type = 0x02;  // BOOT_SWAP_TYPE_TEST
    ret               = ota_storage_write(trailer_swap_info_addr, &swap_type, 1);
    if (ret != RET_OK) {
        LOG_E("OTA_DEBUG", "Write SwapType Failed: %d", ret);
        return ret;
    }

    /* 2.1 校验写入是否成功 */
    uint8_t read_magic[sizeof(MCUBoot_Magic)];
    uint8_t read_swap_type = 0;

    // 简单的内存拷贝读取 (Flash 支持直接映射读取)
    memcpy(read_magic, (void*)trailer_magic_addr, sizeof(MCUBoot_Magic));
    memcpy(&read_swap_type, (void*)trailer_swap_info_addr, 1);

    if (memcmp(read_magic, MCUBoot_Magic, sizeof(MCUBoot_Magic)) != 0) {
        LOG_E("OTA_DEBUG", "Magic Verify Failed! Read: %02X...", read_magic[0]);
        return OTA_ERR_DATA(RET_R_CHECKSUM);
    }

    if (read_swap_type != 0x02) {
        LOG_E("OTA_DEBUG", "SwapType Verify Failed! Read: %02X", read_swap_type);
        return OTA_ERR_DATA(RET_R_CHECKSUM);
    }

    /* 3. 上锁 Flash */
    return ota_storage_finalize();
}

/* =======================================================================
 * OTA 升级确认机制
 *
 * 原理：
 * 1. 在 ota_manager_finish() 中，除了写入 Magic，还在 Backup SRAM 或
 *    一个特定的 Flash 地址写入一个 "pending_upgrade" 标志
 * 2. App 启动时调用 ota_manager_check_if_upgraded() 检查该标志
 * 3. 如果标志存在，说明是刚升级的固件首次启动
 * 4. 验证功能正常后，调用 ota_manager_confirm_upgrade() 清除标志
 *
 * 简化实现：使用 Secondary Slot 的 Magic 区域作为标志
 * - 如果 Slot 1 的 Magic 区域是空的 (0xFF)，说明升级已完成（Bootloader 已处理）
 * - 如果还有 Magic，说明还没有升级
 * ======================================================================= */

bool ota_manager_check_if_upgraded(void) {
    /* 检查 Secondary Slot 的 Magic 区域是否已被清除
     * 如果被清除（全 0xFF），说明 Bootloader 已经完成了升级（把数据搬走了）
     * 这意味着当前运行的就是新固件
     */
    const uint32_t trailer_magic_addr = OTA_STORAGE_SLOT1_START_ADDR + OTA_STORAGE_SLOT1_SIZE - 16;
    uint8_t magic_area[16];
    memcpy(magic_area, (void*)trailer_magic_addr, sizeof(magic_area));

    /* 如果 Magic 区域是空的，说明升级刚完成 */
    bool is_empty = true;
    for (int i = 0; i < sizeof(magic_area); i++) {
        if (magic_area[i] != 0xFF) {
            is_empty = false;
            break;
        }
    }

    /* 同时检查 Magic 是否仍然是我们写入的（还没被 Bootloader 处理）*/
    bool has_pending_magic = (memcmp(magic_area, MCUBoot_Magic, sizeof(MCUBoot_Magic)) == 0);

    /* 如果 Magic 区域为空（被 Bootloader 擦除了），说明升级刚完成 */
    if (is_empty) {
        LOG_I("OTA", "Upgrade detected: Slot 1 Magic area is empty (upgraded)");
        return true;
    }

    /* 如果 Magic 还在，说明还没升级或者正在等待升级 */
    if (has_pending_magic) {
        LOG_I("OTA", "Pending upgrade detected (Magic still present)");
        return false;
    }

    return false;
}

void ota_manager_confirm_upgrade(void) {
    LOG_I("OTA", "Confirming upgrade...");

/*
 * 对于 MCUboot Swap 模式的 Test 升级，必须写入 image_ok=0x01 到
 * Primary Slot 的 trailer 区域，否则下次重启会回滚。
 *
 * Trailer 布局 (末尾 48 字节)：
 * - Magic:     16 bytes (offset -16)
 * - image_ok:  1 byte   (offset -24)
 * - copy_done: 1 byte   (offset -32)
 * - swap_info: 1 byte   (offset -40)
 * - swap_size: 4 bytes  (offset -48)
 *
 * image_ok 位置 = Slot0_End - 24 (但需要按 BOOT_MAX_ALIGN 对齐)
 * MCUboot 使用 BOOT_MAX_ALIGN = 8 或 4, 我们假设 4
 */

/* Primary Slot 地址 */
#define PRIMARY_SLOT_START 0x08020000
#define PRIMARY_SLOT_SIZE  (384 * 1024) /* 384KB */
#define PRIMARY_SLOT_END   (PRIMARY_SLOT_START + PRIMARY_SLOT_SIZE)

    /* image_ok 在 trailer 中的偏移 (参考 MCUboot bootutil_misc.c) */
    /* 对于 BOOT_MAX_ALIGN=4:
     *   image_ok_off = slot_size - 24 (对齐到 4 字节边界)
     * 但实际上 MCUboot 的计算更复杂，我们直接用简化公式
     */
    const uint32_t image_ok_addr = PRIMARY_SLOT_END - 24;

    /* 读取当前值 */
    uint8_t current_val          = *(volatile uint8_t*)image_ok_addr;
    LOG_I("OTA", "Current image_ok at 0x%08lX = 0x%02X", image_ok_addr, current_val);

    if (current_val == 0x01) {
        LOG_I("OTA", "image_ok already set, no action needed.");
        return;
    }

    /* 写入 image_ok = 0x01 */
    ret_code_t ret = ota_storage_init();
    if (ret != RET_OK) {
        LOG_E("OTA", "Failed to init storage: %d", ret);
        return;
    }

    /* 注意：Flash 写入需要 4 字节对齐，我们需要写入一个对齐的 word */
    /* image_ok 地址可能不是 4 字节对齐的，需要特殊处理 */
    uint32_t aligned_addr = image_ok_addr & ~0x3UL; /* 向下对齐到 4 字节 */
    uint32_t byte_offset  = image_ok_addr - aligned_addr;

    /* 读取当前的 4 字节 */
    uint32_t word         = *(volatile uint32_t*)aligned_addr;

    /* 只能将 0xFF 改为其他值（Flash 只能从 1 变为 0）*/
    /* 修改对应字节为 0x01 */
    uint8_t* byte_ptr     = (uint8_t*)&word;
    byte_ptr[byte_offset] = 0x01;

    /* 写入 */
    ret                   = ota_storage_write(aligned_addr, (const uint8_t*)&word, 4);
    if (ret != RET_OK) {
        LOG_E("OTA", "Failed to write image_ok: %d", ret);
        ota_storage_finalize();
        return;
    }

    ota_storage_finalize();

    /* 验证 */
    uint8_t verify_val = *(volatile uint8_t*)image_ok_addr;
    if (verify_val == 0x01) {
        LOG_I("OTA", "Upgrade confirmed! image_ok=0x01 written successfully.");
    } else {
        LOG_E("OTA", "Verification failed! Read back: 0x%02X", verify_val);
    }
}
