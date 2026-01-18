#include "board_gpio_map.h"

#include <stdint.h>

#include "board_gpio_ids.h"
#include "platform_config.h"

#if defined(USE_STM32_HAL)
#include "stm32_hal.h"

typedef struct {
    uint32_t id;
    board_gpio_hw_t hw;
} board_gpio_map_entry_t;

/* 稀疏映射表：ID 不连续时避免浪费空间 */
static const board_gpio_map_entry_t s_map[] = {
    {.id = HAL_GPIO_ID_LED1, .hw = {.port = GPIOC, .pin = 13}},
    {.id = HAL_GPIO_ID_BTN1, .hw = {.port = GPIOA, .pin = 0}},
};

ret_code_t board_gpio_lookup(uint32_t id, board_gpio_hw_t* out) {
    if (!out) return RET_E_INVALID_ARG;
    for (uint32_t i = 0; i < (uint32_t)(sizeof(s_map) / sizeof(s_map[0])); ++i) {
        if (s_map[i].id == id) {
            if (!s_map[i].hw.port || s_map[i].hw.pin >= 16u) return RET_E_NOT_FOUND;
            *out = s_map[i].hw;
            return RET_OK;
        }
    }
    return RET_E_NOT_FOUND;
}
#else
/* 非 STM32 HAL 平台：不提供映射 */
ret_code_t board_gpio_lookup(uint32_t id, board_gpio_hw_t* out) {
    (void)id;
    (void)out;
    return RET_E_UNSUPPORTED;
}
#endif
