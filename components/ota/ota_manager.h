#ifndef SMARTCLOCK_OTA_MANAGER_H
#define SMARTCLOCK_OTA_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#include "ret_code.h"

/* =======================================================================
 * OTA 管理器 API
 * 用于通过 MCUBoot 进行固件更新的高层状态机
 * ======================================================================= */

/**
 * @brief 准备下载 (擦除 Slot 1)
 *
 * @note 这会导致系统冻结 (Freeze) 几秒钟！
 * @param image_size 新镜像的总大小 (用于校验)
 * @return RET_OK 成功, 其它为错误码
 */
ret_code_t ota_manager_start(uint32_t image_size);

/**
 * @brief 写入数据块到 Slot 1
 *
 * @param offset 距离镜像起始位置的偏移量 (从 0 开始)
 * @param data 数据指针
 * @param len 数据长度
 * @return RET_OK 成功, 其它为错误码
 */
ret_code_t ota_manager_write_chunk(uint32_t offset, const uint8_t* data, uint32_t len);

/**
 * @brief 结束下载并标记为 Pending (写入 Magic)
 *
 * @note 请在所有数据写入完成后调用此函数。
 *       它会将 MCUBoot Magic 写入 Slot 1 末尾，通知 Bootloader 测试此镜像。
 *
 * @return RET_OK 成功, 其它为错误码
 */
ret_code_t ota_manager_finish(void);

/**
 * @brief 检查是否刚完成 OTA 升级（新固件首次启动）
 *
 * @return true 如果是升级后首次启动, false 否则
 */
bool ota_manager_check_if_upgraded(void);

/**
 * @brief 确认升级成功（清除升级标记）
 *
 * @note 在确认新固件工作正常后调用此函数。
 *       对于 Swap 模式，这还会调用 boot_set_confirmed() 防止回滚。
 */
void ota_manager_confirm_upgrade(void);

#endif  // SMARTCLOCK_OTA_MANAGER_H
