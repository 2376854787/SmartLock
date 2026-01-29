#ifndef BLACKBOX_PORT_RESET_REASON_AND_CLEAR_FLAGS_H
#define BLACKBOX_PORT_RESET_REASON_AND_CLEAR_FLAGS_H
#include <stdint.h>

#include "blackbox_record.h"
#include "stm32_hal.h"

bb_reset_reason_t BB_Port_ReadResetReasonAndClearFlags(void);
#endif
