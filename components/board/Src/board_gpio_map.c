#include <stdint.h>

#include "../Include/board_gpio_ids.h"
#include "stm32f4xx.h"
/* 与 port 层声明一致 */
typedef struct {
    GPIO_TypeDef *port; /* GPIO端口 */
    uint16_t pin;       /* 0..15 */
} board_gpio_hw_t;

/* 映射表：id -> (port, pin) */
static const board_gpio_hw_t s_map[] = {
    [HAL_GPIO_ID_LED1] = {.port = GPIOC, .pin = 13},
    [HAL_GPIO_ID_BTN1] = {.port = GPIOA, .pin = 0},
};

const board_gpio_hw_t *board_gpio_lookup(uint32_t id) {
    if (id >= (uint32_t)(sizeof(s_map) / sizeof(s_map[0]))) {
        return 0;
    }
    if (s_map[id].port == 0) {
        return 0;
    }
    return &s_map[id];
}
