#ifndef HAL_WDG_PORT_H
#define HAL_WDG_PORT_H

#include "hal_wdg.h"
#include "ret_code_t.h"
#ifdef __cplusplus
extern "C" {
#endif

ret_code_t hal_wdg_port_init(const hal_wdg_cfg_t *cfg);
ret_code_t hal_wdg_port_kick(void);

#ifdef __cplusplus
}
#endif

#endif