//
// Created by yan on 2025/11/15.
//

#include "Beep.h"

#include "main.h"
#include "stm32f4xx_hal_gpio.h"

void Beep_control(const uint8_t  state) {
    if (state) HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_SET);
    else HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_RESET);
}
