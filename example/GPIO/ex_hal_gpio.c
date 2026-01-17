
#include "board_gpio_ids.h"
#include "hal_gpio.h"

hal_gpio_t *led = 0;

void app_init(void) {
    hal_gpio_cfg_t cfg = {.dir           = HAL_GPIO_DIR_OUT,
                          .out_type      = HAL_GPIO_OUT_PP,
                          .pull          = HAL_GPIO_PULL_NONE,
                          .speed         = HAL_GPIO_SPEED_LOW,
                          .irq           = HAL_GPIO_IRQ_NONE,
                          .alternate     = HAL_GPIO_AF_NONE,  // 表示“不启用 AF”
                          .default_level = HAL_GPIO_LEVEL_LOW};

    hal_gpio_open(&led, HAL_GPIO_ID_LED1);
    hal_gpio_config(led, &cfg);
}

void app_loop(void) {
    hal_gpio_toggle(led);
}
