#include "board_gpio_map.h"

#include <stdint.h>

#include "board_gpio_ids.h"
#include "stm32_hal.h"

typedef struct {
    uint32_t id;
    board_gpio_hw_t hw;
} board_gpio_map_entry_t;

/*稀疏映射表：避免在 id 非连续时浪费空间。*/
static const board_gpio_map_entry_t s_map[] = {
    {.id = HAL_GPIO_ID_LED1, .hw = {.port = GPIOC, .pin = 13}},
    {.id = HAL_GPIO_ID_BTN1, .hw = {.port = GPIOA, .pin = 0}},
};

/**
 *
 * @param id GPIO 映射ID
 * @param out 输出查找到的GPIO
 * @return 状态码
 */
ret_code_t board_gpio_lookup(uint32_t id, board_gpio_hw_t *out) {
    /* 检查参数是否有效 */
    if (!out) return RET_E_INVALID_ARG;
    /* 在数组中查找 该ID 对应的端口和PIN */
    for (uint32_t i = 0; i < (uint32_t)(sizeof(s_map) / sizeof(s_map[0])); ++i) {
        /* 找到了 */
        if (s_map[i].id == id) {
            /* 检查参数是否合法 */
            if (!s_map[i].hw.port || s_map[i].hw.pin >= 16u) return RET_E_NOT_FOUND;
            *out = s_map[i].hw;
            return RET_OK;
        }
    }
    return RET_E_NOT_FOUND;
}
