#include <stdint.h>
#include <string.h>

#include "hal_gpio.h"
#include "log.h"
#include "ret_code.h"
#include "main.h"

#include "stm32f4xx_hal_gpio.h"
#include "stm32f4xx_hal_rcc.h"

static ret_code_t gpio_enable_clock(GPIO_TypeDef *GPIOx) {
    if (GPIOx == GPIOA) {
        __HAL_RCC_GPIOA_CLK_ENABLE();
        return RET_OK;
    }
    if (GPIOx == GPIOB) {
        __HAL_RCC_GPIOB_CLK_ENABLE();
        return RET_OK;
    }
    if (GPIOx == GPIOC) {
        __HAL_RCC_GPIOC_CLK_ENABLE();
        return RET_OK;
    }
    if (GPIOx == GPIOD) {
        __HAL_RCC_GPIOD_CLK_ENABLE();
        return RET_OK;
    }
    if (GPIOx == GPIOE) {
        __HAL_RCC_GPIOE_CLK_ENABLE();
        return RET_OK;
    }
    if (GPIOx == GPIOF) {
        __HAL_RCC_GPIOF_CLK_ENABLE();
        return RET_OK;
    }
    if (GPIOx == GPIOG) {
        __HAL_RCC_GPIOG_CLK_ENABLE();
        return RET_OK;
    }
    if (GPIOx == GPIOH) {
        __HAL_RCC_GPIOH_CLK_ENABLE();
        return RET_OK;
    }
    return RET_E_INVALID_ARG;
}

static ret_code_t map_mode(hal_gpio_mode_t m, uint32_t *out_mode) {
    if (out_mode == NULL) {
        return RET_E_INVALID_ARG;
    }

    switch (m) {
        case HAL_GPIO_MODE_INPUT: *out_mode = GPIO_MODE_INPUT;
            return RET_OK;
        case HAL_GPIO_MODE_OUTPUT_PP: *out_mode = GPIO_MODE_OUTPUT_PP;
            return RET_OK;
        case HAL_GPIO_MODE_OUTPUT_OD: *out_mode = GPIO_MODE_OUTPUT_OD;
            return RET_OK;
        case HAL_GPIO_MODE_AF_PP: *out_mode = GPIO_MODE_AF_PP;
            return RET_OK;
        case HAL_GPIO_MODE_AF_OD: *out_mode = GPIO_MODE_AF_OD;
            return RET_OK;
        case HAL_GPIO_MODE_IT_RISING: *out_mode = GPIO_MODE_IT_RISING;
            return RET_OK;
        case HAL_GPIO_MODE_IT_FALLING: *out_mode = GPIO_MODE_IT_FALLING;
            return RET_OK;
        case HAL_GPIO_MODE_IT_BOTH: *out_mode = GPIO_MODE_IT_RISING_FALLING;
            return RET_OK;
        default:
            return RET_E_INVALID_ARG;
    }
}

static ret_code_t map_pull(hal_gpio_pull_t p, uint32_t *out_pull) {
    if (out_pull == NULL) {
        return RET_E_INVALID_ARG;
    }

    switch (p) {
        case HAL_GPIO_PULL_NONE: *out_pull = GPIO_NOPULL;
            return RET_OK;
        case HAL_GPIO_PULL_UP: *out_pull = GPIO_PULLUP;
            return RET_OK;
        case HAL_GPIO_PULL_DOWN: *out_pull = GPIO_PULLDOWN;
            return RET_OK;
        default:
            return RET_E_INVALID_ARG;
    }
}

static ret_code_t map_speed(hal_gpio_speed_t s, uint32_t *out_speed) {
    if (out_speed == NULL) {
        return RET_E_INVALID_ARG;
    }

    switch (s) {
        case HAL_GPIO_SPEED_LOW: *out_speed = GPIO_SPEED_FREQ_LOW;
            return RET_OK;
        case HAL_GPIO_SPEED_MEDIUM: *out_speed = GPIO_SPEED_FREQ_MEDIUM;
            return RET_OK;
        case HAL_GPIO_SPEED_HIGH: *out_speed = GPIO_SPEED_FREQ_HIGH;
            return RET_OK;
        case HAL_GPIO_SPEED_VERY_HIGH: *out_speed = GPIO_SPEED_FREQ_VERY_HIGH;
            return RET_OK;
        default:
            return RET_E_INVALID_ARG;
    }
}

/*
 * Alternate 映射规则（兼容两种输入）：
 * 1) 若上层传入 HAL 语义宏（GPIO_AF7_USART2 等），则 IS_GPIO_AF() 校验通过直接使用
 * 2) 若上层传入 AF 编号（0..15），也允许直接使用（更平台无关）
 * 3) 其他值视为非法
 */
static ret_code_t map_alternate(uint32_t in_af, uint32_t *out_af) {
    if (out_af == NULL) {
        return RET_E_INVALID_ARG;
    }

#ifdef IS_GPIO_AF
    if (IS_GPIO_AF(in_af)) {
        *out_af = in_af;
        return RET_OK;
    }
#endif

    if (in_af <= 15u) {
        *out_af = in_af;
        return RET_OK;
    }

    return RET_E_INVALID_ARG;
}

ret_code_t hal_gpio_init(void *GPIOx, uint16_t pin, const hal_gpio_config_t *config) {
    if (GPIOx == NULL || config == NULL || pin >= 16) {
        LOG_E("GPIO", "init invalid arg");
        return RET_E_INVALID_ARG;
    }

    GPIO_TypeDef *port = (GPIO_TypeDef *) GPIOx;
    ret_code_t rc = gpio_enable_clock(port);
    if (rc != RET_OK) {
        LOG_E("GPIO", "enable clock failed");
        return rc;
    }

    uint32_t mode = 0;
    uint32_t pull = 0;
    uint32_t speed = 0;

    rc = map_mode(config->mode, &mode);
    if (rc != RET_OK) {
        LOG_E("GPIO", "init invalid mode");
        return rc;
    }

    rc = map_pull(config->pull, &pull);
    if (rc != RET_OK) {
        LOG_E("GPIO", "init invalid pull");
        return rc;
    }

    rc = map_speed(config->speed, &speed);
    if (rc != RET_OK) {
        LOG_E("GPIO", "init invalid speed");
        return rc;
    }

    GPIO_InitTypeDef init;
    memset(&init, 0, sizeof(init));

    init.Pin = (uint16_t) (1u << pin);
    init.Mode = mode;
    init.Pull = pull;
    init.Speed = speed;

    /*
     * 注意：
     * - 仅当 AF 模式时才校验/设置 Alternate
     * - 非 AF 模式下 Alternate 置 0，避免结构体残留导致不可预期行为
     */
    if (config->mode == HAL_GPIO_MODE_AF_PP || config->mode == HAL_GPIO_MODE_AF_OD) {
        uint32_t af = 0;
        rc = map_alternate((uint32_t) config->alternate, &af);
        if (rc != RET_OK) {
            LOG_E("GPIO", "init invalid alternate");
            return rc;
        }
        init.Alternate = af;
    } else {
        init.Alternate = 0;
    }

    HAL_GPIO_Init(port, &init);
    return RET_OK;
}

/**
 * @brief gpio写控制
 * @param GPIOx 不透明指针
 * @param pin   GPIO引脚填写数字[0-15]
 * @param level 高低电平
 * @note  必须传入 0-15 且不能空指针
 */
ret_code_t hal_gpio_write(void *GPIOx, uint16_t pin, hal_gpio_level_t level) {
    if (GPIOx == NULL || pin >= 16) {
        LOG_E("GPIO", "write invalid arg");
        return RET_E_INVALID_ARG;
    }
    HAL_GPIO_WritePin((GPIO_TypeDef *) GPIOx, (uint16_t) (1u << pin), (GPIO_PinState) level);
    return RET_OK;
}

/**
 * @brief gpio读控制
 * @param GPIOx 不透明指针
 * @param pin   GPIO引脚填写数字[0-15]
 * @note  必须传入 0-15 且不能空指针
 */
ret_code_t hal_gpio_read(void *GPIOx, uint16_t pin, hal_gpio_level_t *out_level) {
    if (GPIOx == NULL || out_level == NULL || pin >= 16) {
        LOG_E("GPIO", "read invalid arg");
        return RET_E_INVALID_ARG;
    }
    *out_level = (hal_gpio_level_t) HAL_GPIO_ReadPin((GPIO_TypeDef *) GPIOx, (uint16_t) (1u << pin));
    return RET_OK;
}

/**
 * @brief gpio 引脚翻转
 * @param GPIOx 不透明指针
 * @param pin   GPIO引脚填写数字[0-15]
 * @note  必须传入 0-15 且不能空指针
 */
ret_code_t hal_gpio_toggle(void *GPIOx, uint16_t pin) {
    if (GPIOx == NULL || pin >= 16) {
        LOG_E("GPIO", "toggle invalid arg");
        return RET_E_INVALID_ARG;
    }
    HAL_GPIO_TogglePin((GPIO_TypeDef *) GPIOx, (uint16_t) (1u << pin));
    return RET_OK;
}
