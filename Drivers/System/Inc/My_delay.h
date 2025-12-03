//
// Created by yan on 2025/11/22.
//

#ifndef BASE_MY_DELAY_H
#define BASE_MY_DELAY_H
#include <stdbool.h>

#include "main.h"
void delay_init(uint16_t sysclk);
void delay_us(uint32_t nus) ;
void delay_ms(uint16_t nms);
extern uint32_t delay_osintnesting ;
extern bool delay_osrunning;
#endif //BASE_MY_DELAY_H