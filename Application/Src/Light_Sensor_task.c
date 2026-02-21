//
// Created by yan on 2025/11/15.
//

#include "Light_Sensor_task.h"

#include <stdio.h>

#include "Beep.h"
#include "osal.h"
#include "LightSensor.h"
#include "log.h"

void StartLightSensorTask(void *argument) {
    LightSensor_Init();

    for (;;) {
        LightSensor_Init();
        LightSensor_Data = LightSensor_Read();
        // if (LightSensor_Data<2000) Beep_control(1);
        // else Beep_control(0);
       //char buffer[64];
        //sniprintf(buffer, sizeof(buffer), "当前光敏电阻值为 %u\r\n", (unsigned)LightSensor_Data);
       // uart send path is handled by log/hal uart abstraction.
        //LOG_D("光敏","当前光敏电阻值为 %u\r\n", (unsigned)LightSensor_Data);
        //LOG_HEX("哈哈",LOG_LEVEL_ERROR,"666@",6);
        (void)OSAL_delay_ms(1000); // 1s 读一次，完全够用
    }
}
