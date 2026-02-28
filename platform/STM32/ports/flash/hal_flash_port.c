#include "APP_config.h"

#if defined(CFG_TARGET_PLATFORM_STM32_HAL) && defined(CFG_FEAT_HAL_FLASH) && \
    (CFG_FEAT_HAL_FLASH == 1)

#include <string.h>

#include "assert_cus.h"
#include "hal_flash_port.h"
#include "osal.h"
#include "stm32_hal.h"

#define FLASH_PORT_PARAM(reason_)   RET_MAKE_PARAM(RET_MOD_PORT, RET_SUB_PORT_FLASH, (reason_))
#define FLASH_PORT_STATE(reason_)   RET_MAKE_STATE(RET_MOD_PORT, RET_SUB_PORT_FLASH, (reason_))
#define FLASH_PORT_TIMEOUT(reason_) RET_MAKE_TIMEOUT(RET_MOD_PORT, RET_SUB_PORT_FLASH, (reason_))
#define FLASH_PORT_IO(reason_)      RET_MAKE_IO(RET_MOD_PORT, RET_SUB_PORT_FLASH, (reason_))

#ifndef CFG_PARAM_FLASH_VOLTAGE_RANGE
#define CFG_PARAM_FLASH_VOLTAGE_RANGE FLASH_VOLTAGE_RANGE_3
#endif

#ifndef CFG_PARAM_FLASH_IRQ_PRIO
#define CFG_PARAM_FLASH_IRQ_PRIO 5u
#endif

#ifndef CFG_PARAM_FLASH_IRQ_SUBPRIO
#define CFG_PARAM_FLASH_IRQ_SUBPRIO 0u
#endif

typedef struct {
    uint32_t addr;
    uint32_t size;
    uint32_t sector;
} stm32_flash_sector_t;

typedef enum {
    FLASH_PORT_ASYNC_OP_NONE = 0,
    FLASH_PORT_ASYNC_OP_ERASE,
    FLASH_PORT_ASYNC_OP_WRITE,
} flash_port_async_op_t;

typedef struct {
    bool irq_enabled;
    bool busy;
    flash_port_async_op_t op;
    uint32_t addr;
    uint32_t len;
    const uint8_t *write_src;
    uint32_t write_index;
    volatile uint8_t irq_done;
    volatile uint8_t irq_error;
    volatile uint32_t irq_value;
    hal_flash_port_evt_cb_t evt_cb;
    void *evt_user;
} flash_port_async_ctrl_t;

static const stm32_flash_sector_t s_flash_sectors[] = {
    {0x08000000u, 16u * 1024u, FLASH_SECTOR_0},   {0x08004000u, 16u * 1024u, FLASH_SECTOR_1},
    {0x08008000u, 16u * 1024u, FLASH_SECTOR_2},   {0x0800C000u, 16u * 1024u, FLASH_SECTOR_3},
    {0x08010000u, 64u * 1024u, FLASH_SECTOR_4},   {0x08020000u, 128u * 1024u, FLASH_SECTOR_5},
    {0x08040000u, 128u * 1024u, FLASH_SECTOR_6},  {0x08060000u, 128u * 1024u, FLASH_SECTOR_7},
    {0x08080000u, 128u * 1024u, FLASH_SECTOR_8},  {0x080A0000u, 128u * 1024u, FLASH_SECTOR_9},
    {0x080C0000u, 128u * 1024u, FLASH_SECTOR_10}, {0x080E0000u, 128u * 1024u, FLASH_SECTOR_11},
};

static flash_port_async_ctrl_t s_flash_async;

static uint32_t flash_total_size(void) {
    return ((uint32_t)(*(volatile uint16_t *)FLASHSIZE_BASE)) * 1024u;
}

static uint32_t flash_end_addr(void) {
    return FLASH_BASE + flash_total_size();
}

static ret_code_t flash_check_range(uint32_t addr, uint32_t len) {
    REQUIRE_RET(len != 0u, FLASH_PORT_PARAM(RET_R_RANGE_ERR));
    /* 必须大于等于起始地址 查询的长度不能大于总长度*/
    if (addr < FLASH_BASE) return FLASH_PORT_PARAM(RET_R_RANGE_ERR);
    if ((UINT32_MAX - addr) < len) return FLASH_PORT_PARAM(RET_R_RANGE_ERR);
    if ((addr + len) > flash_end_addr()) return FLASH_PORT_PARAM(RET_R_RANGE_ERR);
    return RET_OK;
}

static ret_code_t flash_find_sector(uint32_t addr, hal_flash_region_t *out) {
    REQUIRE_RET(out != NULL, FLASH_PORT_PARAM(RET_R_NULL_PTR));
    if (addr < FLASH_BASE || addr >= flash_end_addr()) return FLASH_PORT_PARAM(RET_R_RANGE_ERR);

    for (uint32_t i = 0; i < (uint32_t)(sizeof(s_flash_sectors) / sizeof(s_flash_sectors[0]));
         i++) {
        const uint32_t start = s_flash_sectors[i].addr;
        const uint32_t end   = start + s_flash_sectors[i].size;
        if (start >= flash_end_addr()) break;
        if ((addr >= start) && (addr < end)) {
            out->index = i;
            out->addr  = start;
            out->size  = s_flash_sectors[i].size;
            return RET_OK;
        }
    }
    return FLASH_PORT_PARAM(RET_R_RANGE_ERR);
}

static ret_code_t flash_check_erased_internal(uint32_t addr, uint32_t len, bool *out) {
    REQUIRE_RET(out != NULL, FLASH_PORT_PARAM(RET_R_NULL_PTR));
    const ret_code_t rc = flash_check_range(addr, len);
    if (ret_is_err(rc)) return rc;

    const uintptr_t raw = (uintptr_t)addr;

    /* 8 字节对齐时按 64/32/16 字节块快速检查。 */
    if (((raw | (uintptr_t)len) & 0x7u) == 0u) {
        const uint64_t *p64  = (const uint64_t *)raw;
        const uint64_t ff64  = UINT64_C(0xFFFFFFFFFFFFFFFF);
        uint32_t count64     = len / 8u;

        while (count64 >= 8u) {
            if ((p64[0] != ff64) || (p64[1] != ff64) || (p64[2] != ff64) || (p64[3] != ff64) ||
                (p64[4] != ff64) || (p64[5] != ff64) || (p64[6] != ff64) || (p64[7] != ff64)) {
                *out = false;
                return RET_OK;
            }
            p64 += 8;
            count64 -= 8u;
        }
        if (count64 >= 4u) {
            if ((p64[0] != ff64) || (p64[1] != ff64) || (p64[2] != ff64) || (p64[3] != ff64)) {
                *out = false;
                return RET_OK;
            }
            p64 += 4;
            count64 -= 4u;
        }
        if (count64 >= 2u) {
            if ((p64[0] != ff64) || (p64[1] != ff64)) {
                *out = false;
                return RET_OK;
            }
            p64 += 2;
            count64 -= 2u;
        }
        if ((count64 != 0u) && (p64[0] != ff64)) {
            *out = false;
            return RET_OK;
        }
        *out = true;
        return RET_OK;
    }

    /* 4 字节对齐时按 64/32/16 字节块快速检查。 */
    if (((raw | (uintptr_t)len) & 0x3u) == 0u) {
        const uint32_t *p32  = (const uint32_t *)raw;
        const uint32_t ff32  = 0xFFFFFFFFu;
        uint32_t count32     = len / 4u;

        while (count32 >= 16u) {
            if ((p32[0] != ff32) || (p32[1] != ff32) || (p32[2] != ff32) || (p32[3] != ff32) ||
                (p32[4] != ff32) || (p32[5] != ff32) || (p32[6] != ff32) || (p32[7] != ff32) ||
                (p32[8] != ff32) || (p32[9] != ff32) || (p32[10] != ff32) || (p32[11] != ff32) ||
                (p32[12] != ff32) || (p32[13] != ff32) || (p32[14] != ff32) || (p32[15] != ff32)) {
                *out = false;
                return RET_OK;
            }
            p32 += 16;
            count32 -= 16u;
        }
        if (count32 >= 8u) {
            if ((p32[0] != ff32) || (p32[1] != ff32) || (p32[2] != ff32) || (p32[3] != ff32) ||
                (p32[4] != ff32) || (p32[5] != ff32) || (p32[6] != ff32) || (p32[7] != ff32)) {
                *out = false;
                return RET_OK;
            }
            p32 += 8;
            count32 -= 8u;
        }
        if (count32 >= 4u) {
            if ((p32[0] != ff32) || (p32[1] != ff32) || (p32[2] != ff32) || (p32[3] != ff32)) {
                *out = false;
                return RET_OK;
            }
            p32 += 4;
            count32 -= 4u;
        }
        while (count32 != 0u) {
            if (*p32 != ff32) {
                *out = false;
                return RET_OK;
            }
            ++p32;
            --count32;
        }
        *out = true;
        return RET_OK;
    }

    const uint8_t *p8 = (const uint8_t *)raw;
    for (uint32_t i = 0; i < len; i++) {
        if (p8[i] != 0xFFu) {
            *out = false;
            return RET_OK;
        }
    }
    *out = true;
    return RET_OK;
}

static ret_code_t flash_map_hal_status(HAL_StatusTypeDef st) {
    if (st == HAL_OK) return RET_OK;
    if (st == HAL_TIMEOUT) return FLASH_PORT_TIMEOUT(RET_R_TIMEOUT);
    if (st == HAL_BUSY) return FLASH_PORT_STATE(RET_R_BUSY);
    return FLASH_PORT_IO(RET_R_FLASH_ERR);
}

static void flash_async_enable_irq_once(void) {
    if (s_flash_async.irq_enabled) return;
    HAL_NVIC_SetPriority(FLASH_IRQn, CFG_PARAM_FLASH_IRQ_PRIO, CFG_PARAM_FLASH_IRQ_SUBPRIO);
    HAL_NVIC_EnableIRQ(FLASH_IRQn);
    s_flash_async.irq_enabled = true;
}

static void flash_async_reset_state(void) {
    s_flash_async.busy       = false;
    s_flash_async.op         = FLASH_PORT_ASYNC_OP_NONE;
    s_flash_async.addr       = 0u;
    s_flash_async.len        = 0u;
    s_flash_async.write_src  = NULL;
    s_flash_async.write_index = 0u;
    s_flash_async.irq_done   = 0u;
    s_flash_async.irq_error  = 0u;
    s_flash_async.irq_value  = 0u;
}

static void flash_async_emit_from_isr(hal_flash_port_evt_t evt) {
    hal_flash_port_evt_cb_t cb = s_flash_async.evt_cb;
    void *user                 = s_flash_async.evt_user;
    if (cb != NULL) cb(user, evt);
}

static void flash_async_complete_from_isr(bool success) {
    (void)HAL_FLASH_Lock();
    flash_async_reset_state();
    flash_async_emit_from_isr(success ? HAL_FLASH_PORT_EVT_DONE : HAL_FLASH_PORT_EVT_ERROR);
}

static void flash_async_drive_from_isr(void) {
    if (!s_flash_async.busy) return;
    if (s_flash_async.irq_error != 0u) {
        s_flash_async.irq_error = 0u;
        flash_async_complete_from_isr(false);
        return;
    }
    if (s_flash_async.irq_done == 0u) return;

    s_flash_async.irq_done = 0u;

    if (s_flash_async.op == FLASH_PORT_ASYNC_OP_ERASE) {
        if (s_flash_async.irq_value != 0xFFFFFFFFu) return;

        bool erased = false;
        const ret_code_t rc = flash_check_erased_internal(s_flash_async.addr, s_flash_async.len, &erased);
        flash_async_complete_from_isr(ret_is_ok(rc) && erased);
        return;
    }

    if (s_flash_async.op == FLASH_PORT_ASYNC_OP_WRITE) {
        if (s_flash_async.write_index < s_flash_async.len) {
            const uint32_t next_addr = s_flash_async.addr + s_flash_async.write_index;
            const uint8_t next_data  = s_flash_async.write_src[s_flash_async.write_index];
            const HAL_StatusTypeDef st =
                HAL_FLASH_Program_IT(FLASH_TYPEPROGRAM_BYTE, next_addr, next_data);
            if (st != HAL_OK) {
                flash_async_complete_from_isr(false);
                return;
            }
            s_flash_async.write_index++;
            return;
        }

        const bool verified =
            (memcmp((const void *)s_flash_async.addr, s_flash_async.write_src, s_flash_async.len) == 0);
        flash_async_complete_from_isr(verified);
    }
}

ret_code_t hal_flash_port_get_info(hal_flash_info_t *out) {
    REQUIRE_RET(out != NULL, FLASH_PORT_PARAM(RET_R_NULL_PTR));
    out->base                       = FLASH_BASE;
    out->total_size                 = flash_total_size();
    out->prog_unit                  = 1u;
    out->erase_value                = 0xFFu;
    out->min_erase_size             = 16u * 1024u;
    out->require_erase_before_write = true;
    return RET_OK;
}

ret_code_t hal_flash_port_get_region(uint32_t addr, hal_flash_region_t *out) {
    return flash_find_sector(addr, out);
}

ret_code_t hal_flash_port_read(uint32_t addr, void *buf, uint32_t len) {
    /* 检查参数 */
    REQUIRE_RET(buf != NULL, FLASH_PORT_PARAM(RET_R_NULL_PTR));
    const ret_code_t rc = flash_check_range(addr, len);
    if (ret_is_err(rc)) return rc;
    /* 复制 */
    memcpy(buf, (const void *)addr, len);
    return RET_OK;
}

ret_code_t hal_flash_port_blank_check(uint32_t addr, uint32_t len, bool *out) {
    return flash_check_erased_internal(addr, len, out);
}

ret_code_t hal_flash_port_set_evt_cb(hal_flash_port_evt_cb_t cb, void *user) {
    osal_crit_state_t cs = 0u;

    OSAL_enter_critical_ex(&cs);
    s_flash_async.evt_cb   = cb;
    s_flash_async.evt_user = user;
    OSAL_exit_critical_ex(cs);
    return RET_OK;
}

ret_code_t hal_flash_port_erase(uint32_t addr, uint32_t len) {
    /* 参数检查 */
    ret_code_t rc = flash_check_range(addr, len);
    if (ret_is_err(rc)) return rc;

    hal_flash_region_t first = {0};
    hal_flash_region_t last  = {0};
    rc                       = flash_find_sector(addr, &first);
    if (ret_is_err(rc)) return rc;
    rc = flash_find_sector((addr + len) - 1u, &last);
    if (ret_is_err(rc)) return rc;

    FLASH_EraseInitTypeDef erase = {0};
    erase.TypeErase              = FLASH_TYPEERASE_SECTORS;
    erase.Sector                 = s_flash_sectors[first.index].sector; /* 起始扇区 */
    erase.NbSectors              = (last.index - first.index) + 1u;     /* 结束扇区 */
    erase.VoltageRange           = CFG_PARAM_FLASH_VOLTAGE_RANGE;       /* 擦除电压 */

    uint32_t sector_error        = 0xFFFFFFFFu;
    /* 解锁 Flash */
    if (HAL_FLASH_Unlock() != HAL_OK) return FLASH_PORT_IO(RET_R_FLASH_ERR);
    /* 清除Flag */
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
    /* 擦除 Flash 指定的部分扇区 */
    const HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&erase, &sector_error);
    /* 上锁Flash */
    (void)HAL_FLASH_Lock();
    if (st != HAL_OK) return flash_map_hal_status(st);
    if (sector_error != 0xFFFFFFFFu) return FLASH_PORT_IO(RET_R_FLASH_ERR);

    bool erased = false;
    /* 检查是否擦除成功 */
    rc          = flash_check_erased_internal(addr, len, &erased);
    if (ret_is_err(rc)) return rc;
    return erased ? RET_OK : FLASH_PORT_IO(RET_R_FLASH_ERR);
}

ret_code_t hal_flash_port_erase_it(uint32_t addr, uint32_t len) {
    ret_code_t rc = flash_check_range(addr, len);
    if (ret_is_err(rc)) return rc;

    hal_flash_region_t first = {0};
    hal_flash_region_t last  = {0};
    rc                       = flash_find_sector(addr, &first);
    if (ret_is_err(rc)) return rc;
    rc = flash_find_sector((addr + len) - 1u, &last);
    if (ret_is_err(rc)) return rc;

    osal_crit_state_t cs = 0u;
    OSAL_enter_critical_ex(&cs);
    if (s_flash_async.busy) {
        OSAL_exit_critical_ex(cs);
        return FLASH_PORT_STATE(RET_R_BUSY);
    }
    s_flash_async.busy       = true;
    s_flash_async.op         = FLASH_PORT_ASYNC_OP_ERASE;
    s_flash_async.addr       = addr;
    s_flash_async.len        = len;
    s_flash_async.write_src  = NULL;
    s_flash_async.write_index = 0u;
    s_flash_async.irq_done   = 0u;
    s_flash_async.irq_error  = 0u;
    s_flash_async.irq_value  = 0u;
    OSAL_exit_critical_ex(cs);

    flash_async_enable_irq_once();

    FLASH_EraseInitTypeDef erase = {0};
    erase.TypeErase              = FLASH_TYPEERASE_SECTORS;
    erase.Sector                 = s_flash_sectors[first.index].sector;
    erase.NbSectors              = (last.index - first.index) + 1u;
    erase.VoltageRange           = CFG_PARAM_FLASH_VOLTAGE_RANGE;

    if (HAL_FLASH_Unlock() != HAL_OK) {
        flash_async_reset_state();
        return FLASH_PORT_IO(RET_R_FLASH_ERR);
    }
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR |
                           FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    const HAL_StatusTypeDef st = HAL_FLASHEx_Erase_IT(&erase);
    if (st != HAL_OK) {
        (void)HAL_FLASH_Lock();
        flash_async_reset_state();
        return flash_map_hal_status(st);
    }
    return RET_OK;
}

ret_code_t hal_flash_port_write(uint32_t addr, const void *data, uint32_t len) {
    /* 参数 检查 */
    REQUIRE_RET(data != NULL, FLASH_PORT_PARAM(RET_R_NULL_PTR));
    ret_code_t rc = flash_check_range(addr, len);
    if (ret_is_err(rc)) return rc;

    bool erased = false;
    /* 擦除检查 */
    rc          = flash_check_erased_internal(addr, len, &erased);
    if (ret_is_err(rc)) return rc;
    if (!erased) return FLASH_PORT_STATE(RET_R_STATE_ERR);

    if (HAL_FLASH_Unlock() != HAL_OK) return FLASH_PORT_IO(RET_R_FLASH_ERR);
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    const uint8_t *src = (const uint8_t *)data;
    for (uint32_t i = 0; i < len; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, addr + i, src[i]) != HAL_OK) {
            (void)HAL_FLASH_Lock();
            return FLASH_PORT_IO(RET_R_FLASH_ERR);
        }
    }

    (void)HAL_FLASH_Lock();

    if (memcmp((const void *)addr, data, len) != 0) return FLASH_PORT_IO(RET_R_FLASH_ERR);
    return RET_OK;
}

ret_code_t hal_flash_port_write_it(uint32_t addr, const void *data, uint32_t len) {
    REQUIRE_RET(data != NULL, FLASH_PORT_PARAM(RET_R_NULL_PTR));
    ret_code_t rc = flash_check_range(addr, len);
    if (ret_is_err(rc)) return rc;

    bool erased = false;
    rc          = flash_check_erased_internal(addr, len, &erased);
    if (ret_is_err(rc)) return rc;
    if (!erased) return FLASH_PORT_STATE(RET_R_STATE_ERR);

    osal_crit_state_t cs = 0u;
    OSAL_enter_critical_ex(&cs);
    if (s_flash_async.busy) {
        OSAL_exit_critical_ex(cs);
        return FLASH_PORT_STATE(RET_R_BUSY);
    }
    s_flash_async.busy       = true;
    s_flash_async.op         = FLASH_PORT_ASYNC_OP_WRITE;
    s_flash_async.addr       = addr;
    s_flash_async.len        = len;
    s_flash_async.write_src  = (const uint8_t *)data;
    s_flash_async.write_index = 1u;
    s_flash_async.irq_done   = 0u;
    s_flash_async.irq_error  = 0u;
    s_flash_async.irq_value  = 0u;
    OSAL_exit_critical_ex(cs);

    flash_async_enable_irq_once();

    if (HAL_FLASH_Unlock() != HAL_OK) {
        flash_async_reset_state();
        return FLASH_PORT_IO(RET_R_FLASH_ERR);
    }
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR |
                           FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    const HAL_StatusTypeDef st =
        HAL_FLASH_Program_IT(FLASH_TYPEPROGRAM_BYTE, addr, ((const uint8_t *)data)[0]);
    if (st != HAL_OK) {
        (void)HAL_FLASH_Lock();
        flash_async_reset_state();
        return flash_map_hal_status(st);
    }
    return RET_OK;
}

void FLASH_IRQHandler(void) {
    HAL_FLASH_IRQHandler();
    flash_async_drive_from_isr();
}

void HAL_FLASH_EndOfOperationCallback(uint32_t ReturnValue) {
    s_flash_async.irq_value = ReturnValue;
    s_flash_async.irq_done  = 1u;
}

void HAL_FLASH_OperationErrorCallback(uint32_t ReturnValue) {
    s_flash_async.irq_value = ReturnValue;
    s_flash_async.irq_error = 1u;
}

#endif
