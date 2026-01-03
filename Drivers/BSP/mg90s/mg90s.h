#ifndef __MG90S_H__
#define __MG90S_H__

#ifdef __cplusplus
extern "C" {

#endif

#include "main.h"

/* 
 * Mg90s Servo Driver
 * 
 * Connection:
 * - Signal Pin: PC6 (TIM3 CH1) - Configurable in mg90s.c
 * - VCC: 5V
 * - GND: GND
 * 
 * Logic:
 * - PWM Frequency: 50Hz (Period 20ms)
 * - Pulse Width: 0.5ms (0 deg) to 2.5ms (180 deg) typically.
 *   - Lock Position: 0 degree (approx 0.5ms)
 *   - Unlock Position: 90 degree (approx 1.5ms)
 */

void mg90s_init(void);

void mg90s_unlock(void);

void mg90s_lock(void);

void MX_TIM3_Init_Custom(void);

#ifdef __cplusplus
}
#endif

#endif /* __MG90S_H__ */
