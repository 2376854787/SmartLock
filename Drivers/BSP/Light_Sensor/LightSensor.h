//
// Created by yan on 2025/11/15.
//

#ifndef SMARTCLOCK_LIGHTSENSOR_H
#define SMARTCLOCK_LIGHTSENSOR_H
#include <stdint.h>
extern uint16_t LightSensor_Data;
void LightSensor_Init(void);
uint16_t LightSensor_Read(void);
#endif //SMARTCLOCK_LIGHTSENSOR_H