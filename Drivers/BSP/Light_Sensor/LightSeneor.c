//
// Created by yan on 2025/11/15.
//
#include "adc.h"
#include "LightSensor.h"
#include "stm32f4xx_hal_adc.h"
uint16_t LightSensor_Data=0;
void LightSensor_Init(void)
{
    HAL_ADC_Start(&hadc3);
}

uint16_t LightSensor_Read(void)
{
    // 等待转换完成（超时给个小值，比如10ms）
    if (HAL_ADC_PollForConversion(&hadc3, 10) == HAL_OK)
    {
        return HAL_ADC_GetValue(&hadc3);
    }
    return 0; // 出错就返回0
}