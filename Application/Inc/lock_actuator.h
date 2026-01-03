#ifndef LOCK_ACTUATOR_H
#define LOCK_ACTUATOR_H

#include <stdbool.h>
#include <stdint.h>

#include "APP_config.h"

#ifndef LOCK_ACTUATOR_UNLOCK_HOLD_MS
#define LOCK_ACTUATOR_UNLOCK_HOLD_MS 5000u
#endif

void LockActuator_Start(void);
bool LockActuator_IsReady(void);

/* Non-blocking; returns false if queue not ready/full. */
bool LockActuator_LockAsync(void);
bool LockActuator_UnlockAsync(void);
bool LockActuator_UnlockForAsync(uint32_t hold_ms);

#endif /* LOCK_ACTUATOR_H */
