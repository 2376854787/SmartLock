#ifndef SMARTCLOCK_WATCHDOG_APP_H
#define SMARTCLOCK_WATCHDOG_APP_H

#include "ret_code_t.h"

#ifdef __cplusplus
extern "C" {
#endif
#define CFG_PARAM_WATCHDOG_APP_TIMEOUT_MS    8000u
#define CFG_PARAM_WATCHDOG_APP_SUP_PERIOD_MS 200u
#define CFG_PARAM_WATCHDOG_APP_BOOT_GRACE_MS 8000u

ret_code_t Watchdog_AppInit(void);

#ifdef __cplusplus
}
#endif

#endif  // SMARTCLOCK_WATCHDOG_APP_H
