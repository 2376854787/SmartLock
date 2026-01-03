#ifndef LOCK_DEVICES_H
#define LOCK_DEVICES_H

#include <stdbool.h>
#include <stdint.h>

void LockDevices_Start(void);

bool LockDevices_As608Ready(void);
bool LockDevices_Rc522Ready(void);

bool LockDevices_WaitAs608Ready(uint32_t timeout_ms);
bool LockDevices_WaitRc522Ready(uint32_t timeout_ms);

#endif /* LOCK_DEVICES_H */

