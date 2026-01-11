#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include <stdint.h>
#include <stdbool.h>

/* 引脚电平定义 */
typedef enum {
    HAL_GPIO_LEVEL_LOW = 0,
    HAL_GPIO_LEVEL_HIGH
} hal_gpio_level_t;

/* 引脚模式定义 */
typedef enum {
    HAL_GPIO_MODE_INPUT = 0,
    HAL_GPIO_MODE_OUTPUT_PP,   // 推挽输出
    HAL_GPIO_MODE_OUTPUT_OD,   // 开漏输出
} hal_gpio_mode_t;

/* 引脚上下拉定义 */
typedef enum {
    HAL_GPIO_PULL_NONE = 0,
    HAL_GPIO_PULL_UP,
    HAL_GPIO_PULL_DOWN
} hal_gpio_pull_t;

/**
 * @brief 初始化 GPIO
 * @param GPIOx GPIO句柄
 * @param pin 引脚索引（通常在 port 层映射为具体的 PORT/PIN）
 * @param mode 模式
 * @param pull 上下拉
 * @note 必须在任务上下文中调用
 */
void hal_gpio_init(void *GPIOx, uint16_t pin, hal_gpio_mode_t mode, hal_gpio_pull_t pull);

/**
 * @brief 写引脚电平
 * @note 线程安全，允许在 ISR 中调用
 */
void hal_gpio_write(void *GPIOx, uint16_t pin, hal_gpio_level_t level);

/**
 * @brief 读引脚电平
 * @note 线程安全，允许在 ISR 中调用
 */
void hal_gpio_ReadPin(void *GPIOx, uint16_t pin);

/**
 * @brief 翻转引脚电平
 * @note 线程安全，允许在 ISR 中调用
 */
void hal_gpio_toggle(uint16_t pin_index);

/**
 * @brief 读取引脚电平
 * @return 当前电平
 * @note 线程安全，允许在 ISR 中调用
 */
hal_gpio_level_t hal_gpio_read(uint16_t pin_index);

#endif