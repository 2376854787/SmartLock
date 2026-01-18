#ifndef SMARTLOCK_BOARD_GPIO_MAP_H
#define SMARTLOCK_BOARD_GPIO_MAP_H

#include <stdint.h>

#include "ret_code.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 端口层可开启的最大 GPIO 句柄数 */
#define BOARD_GPIO_MAP_MAX 64u
typedef struct {
    void* port;   /* GPIO 端口句柄（平台相关） */
    uint16_t pin; /* 0..15 */
} board_gpio_hw_t;

ret_code_t board_gpio_lookup(uint32_t id, board_gpio_hw_t* out);

#ifdef __cplusplus
}
#endif

#endif  // SMARTLOCK_BOARD_GPIO_MAP_H
