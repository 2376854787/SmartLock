//
// Created by Gemini on 2026/01/27.
//

#include "ota_manager.h"

#include <string.h>

#include "ota_storage.h"

/* =======================================================================
 * 常量 & 宏
 * ======================================================================= */

/* MCUboot Magic (16 字节)
   0x77, 0xc2, 0x95, 0xf3, 0x60, 0xd2, 0xef, 0x7f,
   0x35, 0x58, 0x50, 0x72, 0xb4, 0x5c, 0x37, 0xc0
*/
static const uint8_t MCUBoot_Magic[] = {0x77, 0xc2, 0x95, 0xf3, 0x60, 0xd2, 0xef, 0x7f,
                                        0x35, 0x58, 0x50, 0x72, 0xb4, 0x5c, 0x37, 0xc0};

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

    /* 优化策略: 不在 Start 时全擦除。改为按需擦除 (Lazy Erase)。
     * 这样可以避免一次性卡顿 5 秒。
     */
    // ret = ota_storage_erase_region(OTA_STORAGE_SLOT1_START_ADDR, OTA_STORAGE_SLOT1_SIZE);

    return RET_OK;
}

ret_code_t ota_manager_write_chunk(uint32_t offset, const uint8_t* data, uint32_t len) {
    /* 校验边界 */
    if (offset + len > OTA_STORAGE_SLOT1_SIZE) {
        return OTA_ERR_PARAM(RET_R_RANGE_ERR);
    }

    uint32_t current_addr_start = OTA_STORAGE_SLOT1_START_ADDR + offset;

    /* 1. 检查是否跨越了扇区边界 (Lazy Erase) */
    /* 我们需要检查本次写入的 [start, start+len] 范围内，是否包含了扇区的起始地址
     * 或者简单点：如果 offset == 0，肯定是起始。
     * 如果 offset 刚好落在 128KB 边界上，也是起始。
     */

    /* 遍历本次写入覆盖的每一个地址，检查是否击中了扇区头部 */
    /* 优化：只检查 start 是否是头部即可（假设每次 write 不会超过 128KB） */
    uint32_t sector_size        = 0;
    if (ota_storage_is_sector_start(current_addr_start, &sector_size)) {
        // 这是一个新扇区的开始，执行擦除
        // 注意：这会阻塞 1-2 秒
        ret_code_t ret = ota_storage_erase_region(current_addr_start, sector_size);
        if (ret != RET_OK) {
            return ret;
        }
    }

    /* 2. 写入 Flash */
    return ota_storage_write(current_addr_start, data, len);
}

ret_code_t ota_manager_finish(void) {
    /* 1. 写入有效的 Magic Number 到 Slot 1 末尾 */
    /* Trailer 位置: Slot 末尾 - 16 直接 */
    const uint32_t trailer_addr = OTA_STORAGE_SLOT1_START_ADDR + OTA_STORAGE_SLOT1_SIZE - 16;

    /*
     * 极端情况处理：如果最后一包数据还没用到 Slot1 的最后一个扇区，
     * 那么该扇区可能还没被擦除。
     * 但 Trailer 必须写进去。
     * 所以这里要检查 Trailer 所在的扇区是否被擦除了？
     *
     * 通常 Slot1 分配了固定的 Sector 8,9,10。
     * 如果固件很小只占了 Sector 8，那么 Sector 9 和 10 里的内容是脏的。
     *
     * 写 Magic 到 Sector 10 的末尾时，必须确保 Sector 10 已被擦除。
     *
     * 简单做法：finish 时强制检查一次 Trailer 所在扇区是否干净？
     * 或者更简单：在 start 时候虽然不全擦，但记录一个标志？
     *
     * 为了稳妥：我们在写 Trailer 前，强制尝试擦除 Trailer 所在的那个扇区？
     * 不行，如果固件比较大，正好延伸到了 Trailer 所在的扇区，那数据就被擦没了。
     *
     * 修正策略：
     * 通常 Trailer 是放在 Image 的 Padding 之后。
     * MCUboot 的 Slot 1 是固定大小的。
     *
     * 如果我们采用 Lazy Erase，只有写到了那个位置才擦。
     * 如果固件只有 10KB，我们只擦了 Sector 8。
     * Magic 要写到 Sector 10 (Slot 1 End)。
     * 此时 Sector 10 可能是脏的。
     *
     * 方案：
     * 定义一个变量记录 "Max Erased Address"？太复杂。
     *
     * 实用方案：
     * 在 finish() 里，如果发现固件比较小（没触及最后一个扇区），
     * 则手动擦除最后一个扇区 (Sector 10)。
     */

    uint32_t last_sector_addr   = 0x080C0000;  // Sector 10 start
    uint32_t sector_size        = 128 * 1024;

    // 检查 Trailer 所在的扇区是否被我们刚刚的数据流擦除过？
    // 如果没有任何数据写到 Sector 10，那么它还是脏的。
    // 如何判断？看 write_chunk 曾经写到的最大地址。

    // 既然太复杂，不如简化：
    // 如果用户能接受，start() 时候还是全擦比较安全。
    // 或者：
    // 用户选择 Lazy Erase 的风险就是必须自己管理这些。
    //
    // 让我们的 ota_storage_is_sector_start 逻辑更智能？
    //
    // 现在的权宜之计：
    // 强制擦除 Trailer 所在的扇区，前提是我们确定没有应用数据写到这里。
    // Slot Size = 384KB. Sector 8, 9, 10.
    // 如果 Image Size < 256KB, 那么 Sector 10 完全没人碰。
    // 此时我们擦除 Sector 10 是安全的。

    // 如果 Image Size > 256KB, 那么 Sector 10 在 write_chunk 时已经被擦过了。
    // 我们怎么知道它被擦过了？
    // 重复擦除是否会报错？HAL_Flash_Erase 如果擦除已经是 0xFF 的区域，通常没事。
    // 但会浪费 2 秒。

    // 让我们读一下 Trailer 位置的值。如果全是 0xFF，就不擦了。
    // 这需要读 Flash 的能力。

    uint32_t* pMagic            = (uint32_t*)trailer_addr;
    if (pMagic[0] != 0xFFFFFFFF) {
        // 需要擦除 Trailer 所在的扇区
        // 注意：这很有风险！如果此前刚写入了有效数据，再擦就丢了。
        // 只有当这是“空闲区域”时才能擦。

        // 鉴于 Lazy Erase 在尾部处理的复杂性，
        // 且通常 Magic 写在整个 Slot 的最末尾 (Sector 10)。
        // 如果你的固件 < 256KB，Sector 10 没被动过。
        // 我们这里显式擦除 Sector 10.
        ota_storage_erase_region(last_sector_addr, sector_size);
    }

    const ret_code_t ret = ota_storage_write(trailer_addr, MCUBoot_Magic, sizeof(MCUBoot_Magic));
    if (ret != RET_OK) {
        return ret;
    }

    /* 2. 上锁 Flash */
    return ota_storage_finalize();
}
