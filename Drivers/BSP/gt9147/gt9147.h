#ifndef __GT9147_H
#define __GT9147_H

#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx_hal.h"

/* Pin mapping (using the provided touch connector pins) */
#define GT9147_RST_PORT GPIOC
#define GT9147_RST_PIN  GPIO_PIN_13

#define GT9147_INT_PORT GPIOB
#define GT9147_INT_PIN  GPIO_PIN_1

#define GT9147_SCL_PORT GPIOB
#define GT9147_SCL_PIN  GPIO_PIN_0

#define GT9147_SDA_PORT GPIOF
#define GT9147_SDA_PIN  GPIO_PIN_11

/* 7-bit I2C address (0x5D is the common default for GT9147) */
#ifndef GT9147_I2C_ADDR
#define GT9147_I2C_ADDR 0x5D
#endif

/* Touch panel resolution for coordinate mapping */
#ifndef GT9147_MAX_WIDTH
#define GT9147_MAX_WIDTH  800
#endif

#ifndef GT9147_MAX_HEIGHT
#define GT9147_MAX_HEIGHT 480
#endif

/* Coordinate transform (adjust if touch is rotated or mirrored) */
#ifndef GT9147_SWAP_XY
#define GT9147_SWAP_XY 0
#endif

#ifndef GT9147_INVERT_X
#define GT9147_INVERT_X 0
#endif

#ifndef GT9147_INVERT_Y
#define GT9147_INVERT_Y 0
#endif

bool gt9147_init(void);
bool gt9147_read_point(uint16_t *x, uint16_t *y, bool *pressed);

#endif /* __GT9147_H */
