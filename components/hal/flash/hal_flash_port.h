#ifndef HAL_FLASH_PORT_H
#define HAL_FLASH_PORT_H

#include "APP_config.h"

#if defined(CFG_FEAT_HAL_FLASH) && (CFG_FEAT_HAL_FLASH == 1)

#include "hal_flash.h"

typedef enum {
    HAL_FLASH_PORT_EVT_DONE = 0,
    HAL_FLASH_PORT_EVT_ERROR,
} hal_flash_port_evt_t;

typedef void (*hal_flash_port_evt_cb_t)(void *user, hal_flash_port_evt_t evt);

ret_code_t hal_flash_port_get_info(hal_flash_info_t *out);
ret_code_t hal_flash_port_get_region(uint32_t addr, hal_flash_region_t *out);
ret_code_t hal_flash_port_read(uint32_t addr, void *buf, uint32_t len);
ret_code_t hal_flash_port_erase(uint32_t addr, uint32_t len);
ret_code_t hal_flash_port_write(uint32_t addr, const void *data, uint32_t len);
ret_code_t hal_flash_port_blank_check(uint32_t addr, uint32_t len, bool *out);
ret_code_t hal_flash_port_set_evt_cb(hal_flash_port_evt_cb_t cb, void *user);
ret_code_t hal_flash_port_erase_it(uint32_t addr, uint32_t len);
ret_code_t hal_flash_port_write_it(uint32_t addr, const void *data, uint32_t len);

#endif

#endif
