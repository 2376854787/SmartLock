//
// Created by yan on 2025/11/15.
//

#include "Light_Sensor_task.h"

#include <stdio.h>

#include "Beep.h"
#include "cmsis_os2.h"
#include "LightSensor.h"
#include "log.h"
#include "usart.h"

void StartLightSensorTask(void *argument) {
    LightSensor_Init();

    for (;;) {
        LightSensor_Init();
        LightSensor_Data = LightSensor_Read();
        // if (LightSensor_Data<2000) Beep_control(1);
        // else Beep_control(0);
       //char buffer[64];
        //sniprintf(buffer, sizeof(buffer), "当前光敏电阻值为 %u\r\n", (unsigned)LightSensor_Data);
       // HAL_UART_Transmit_DMA(&huart1, (uint8_t *)buffer, sizeof(LightSensor_Data));
        LOG_D("光敏","当前光敏电阻值为 %u\r\n", (unsigned)LightSensor_Data);
        LOG_HEX("哈哈",LOG_LEVEL_ERROR,"666@",6);
        osDelay(1000); // 1s 读一次，完全够用
    }
}
