#ifndef HAL_PWR_PORT_H
#define HAL_PWR_PORT_H

#include "APP_config.h"

#if defined(CFG_TARGET_PLATFORM_STM32_HAL) && defined(CFG_FEAT_HAL_PWR) && (CFG_FEAT_HAL_PWR == 1)

#include <stdint.h>

#include "hal_pwr.h"
#include "ret_code_t.h"

#ifdef __cplusplus
extern "C" {
#endif

ret_code_t hal_pwr_port_init(void);
ret_code_t hal_pwr_port_deinit(void);
ret_code_t hal_pwr_port_apply_config_set(const hal_pwr_config_set_t *cfg_set);
ret_code_t hal_pwr_port_get_capability(hal_pwr_capability_t *out);
ret_code_t hal_pwr_port_get_reset_raw_value(uint32_t *out_raw_value);
ret_code_t hal_pwr_port_clear_reset_flags(void);
ret_code_t hal_pwr_port_get_wakeup_reason(uint32_t *out_mask);
ret_code_t hal_pwr_port_clear_wakeup_reason(uint32_t mask);
ret_code_t hal_pwr_port_configure_wakeup_source(const hal_pwr_wakeup_source_cfg_t *cfg);
ret_code_t hal_pwr_port_set_mode(hal_pwr_mode_t mode, const hal_pwr_mode_cfg_t *mode_cfg);

#ifdef __cplusplus
}
#endif

#endif

#endif /* HAL_PWR_PORT_H */
