#include <stdint.h>
#include <string.h>

#include "hal_gpio.h"
#include "ret_code.h"


#include "stm32f4xx.h"
#include "stm32f4xx_hal_gpio.h"
#include "stm32f4xx_hal_rcc.h"

/* board 映射表：由 board/board_gpio_map.c 提供 */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin; /* 0..15 */
} board_gpio_hw_t;

const board_gpio_hw_t *board_gpio_lookup(uint32_t id);

/* ---------------- 断言（热路径用） ---------------- */

__attribute__((weak)) void hal_gpio_assert_failed(const char *file, int line) {
    (void) file;
    (void) line;
    __disable_irq();
    while (1) {
        /* fail-stop */
    }
}

#ifndef HAL_GPIO_ASSERT
#define HAL_GPIO_ASSERT(x) do { if (!(x)) hal_gpio_assert_failed(__FILE__, __LINE__); } while (0)
#endif

/* ---------------- 句柄定义（只在 port.c 可见） ---------------- */

struct hal_gpio {
    GPIO_TypeDef *port;
    uint16_t pin; /* 0..15 */
};

/* ---------------- 内部工具 ---------------- */

static inline uint16_t pin_mask(uint16_t pin) {
    return (uint16_t) (1u << pin);
}

/**
 * @brief 开启对应的时钟
 * @param GPIOx GPIOx
 * @return 时钟开启结果
 */
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

/**
 * @brief 上下拉枚举映射
 * @param p
 * @param out
 * @return 被映射的平台上下拉枚举
 */
static ret_code_t map_pull(hal_gpio_pull_t p, uint32_t *out) {
    if (!out) return RET_E_INVALID_ARG;
    switch (p) {
        case HAL_GPIO_PULL_NONE: *out = GPIO_NOPULL;
            return RET_OK;
        case HAL_GPIO_PULL_UP: *out = GPIO_PULLUP;
            return RET_OK;
        case HAL_GPIO_PULL_DOWN: *out = GPIO_PULLDOWN;
            return RET_OK;
        default: return RET_E_INVALID_ARG;
    }
}

/**
 * @brief 速度枚举映射
 * @param s
 * @param out
 * @return
 */
static ret_code_t map_speed(hal_gpio_speed_t s, uint32_t *out) {
    if (!out) return RET_E_INVALID_ARG;
    switch (s) {
        case HAL_GPIO_SPEED_LOW: *out = GPIO_SPEED_FREQ_LOW;
            return RET_OK;
        case HAL_GPIO_SPEED_MEDIUM: *out = GPIO_SPEED_FREQ_MEDIUM;
            return RET_OK;
        case HAL_GPIO_SPEED_HIGH: *out = GPIO_SPEED_FREQ_HIGH;
            return RET_OK;
        case HAL_GPIO_SPEED_VERY_HIGH: *out = GPIO_SPEED_FREQ_VERY_HIGH;
            return RET_OK;
        default: return RET_E_INVALID_ARG;
    }
}

/**
 * @brief 复用功能映射
 * @param in_af
 * @param out_af
 * @return ret_code_t
 */
static ret_code_t map_alternate(uint32_t in_af, uint32_t *out_af) {
    if (!out_af) return RET_E_INVALID_ARG;

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

/* ---------------- 平台导出：供 components/hal/src/hal_gpio.c 调用 ---------------- */
/**
 * @brief 从板级GPIO映射表获取 具体的GPIO
 * @param out 存储具体的port/Pin
 * @param id 板级映射
 * @return 返回状态码
 * @note
 */
ret_code_t hal_gpio_port_open(hal_gpio_t **out, uint32_t id) {
    if (!out) return RET_E_INVALID_ARG;

    const board_gpio_hw_t *hw = board_gpio_lookup(id);
    if (!hw || !hw->port || hw->pin >= 16u) {
        return RET_E_NOT_FOUND;
    }

    static hal_gpio_t handles[64]; /* 简化：静态池，实际可按项目规模调整 */
    static uint8_t used[64] = {0}; /* 标记使用过的GPIO */

    /* 简化策略：id 必须 < 64，直接映射到句柄槽位 */
    if (id >= 64u) return RET_E_INVALID_ARG;
    if (!used[id]) {
        handles[id].port = hw->port;
        handles[id].pin = hw->pin;
        used[id] = 1;
    }

    *out = &handles[id];
    return RET_OK;
}

/**
 * @brief 初始化指定的GPIO
 * @param h GPIO
 * @param cfg 配置结构体
 * @return 运行状态
 */
ret_code_t hal_gpio_port_config(hal_gpio_t *h, const hal_gpio_cfg_t *cfg) {
    /* 检查非空指针 */
    if (!h || !cfg) return RET_E_INVALID_ARG;
    /* 检查合法参数 */
    if (h->pin >= 16u) return RET_E_INVALID_ARG;
    if (cfg->dir >= HAL_GPIO_DIR_MAX ||
        cfg->pull >= HAL_GPIO_PULL_MAX ||
        cfg->speed >= HAL_GPIO_SPEED_MAX ||
        cfg->irq >= HAL_GPIO_IRQ_MAX ||
        cfg->out_type >= HAL_GPIO_OUT_MAX ||
        cfg->default_level >= HAL_GPIO_LEVEL_MAX) {
        return RET_E_INVALID_ARG;
    }

    /* 开启时钟 */
    ret_code_t rc = gpio_enable_clock(h->port);
    if (rc != RET_OK) return rc;

    uint32_t pull = 0, speed = 0;
    /* 映射 上下拉、速度*/
    rc = map_pull(cfg->pull, &pull);
    if (rc != RET_OK) return rc;
    rc = map_speed(cfg->speed, &speed);
    if (rc != RET_OK) return rc;

    /* 默认初始化 */
    GPIO_InitTypeDef init;
    memset(&init, 0, sizeof(init));

    /* 填充引脚、上下拉、速度 */
    init.Pin = pin_mask(h->pin);
    init.Pull = pull;
    init.Speed = speed;

    /* mode 映射：由通用 cfg 映射到 STM32 HAL */
    if (cfg->irq != HAL_GPIO_IRQ_NONE) {
        /* EXTI 触发模式（NVIC 不在这里配置） */
        if (cfg->irq == HAL_GPIO_IRQ_RISING) init.Mode = GPIO_MODE_IT_RISING;
        else if (cfg->irq == HAL_GPIO_IRQ_FALLING) init.Mode = GPIO_MODE_IT_FALLING;
        else init.Mode = GPIO_MODE_IT_RISING_FALLING;
    } else if (cfg->dir == HAL_GPIO_DIR_IN) {
        init.Mode = GPIO_MODE_INPUT;
    } else {
        /* 输出模式：推挽/开漏 */
        init.Mode = (cfg->out_type == HAL_GPIO_OUT_OD) ? GPIO_MODE_OUTPUT_OD : GPIO_MODE_OUTPUT_PP;
    }

    /* AF：只有在你定义为复用的场景才需要，这里用：alternate!=0xFFFFFFFF 作为“启用AF”的开关 */
    if (cfg->alternate != 0xFFFFFFFFu) {
        uint32_t af = 0;
        rc = map_alternate(cfg->alternate, &af);
        if (rc != RET_OK) return rc;

        init.Mode = (cfg->out_type == HAL_GPIO_OUT_OD) ? GPIO_MODE_AF_OD : GPIO_MODE_AF_PP;
        init.Alternate = af;
    }

    HAL_GPIO_Init(h->port, &init);

    /* default_level：仅对“纯输出”生效（AF/中断/输入不写） */
    if (cfg->irq == HAL_GPIO_IRQ_NONE &&
        cfg->dir == HAL_GPIO_DIR_OUT &&
        cfg->alternate == 0xFFFFFFFFu) {
        HAL_GPIO_WritePin(h->port, pin_mask(h->pin), (GPIO_PinState) cfg->default_level);
    }

    return RET_OK;
}

/**
 * @brief 关闭GPIO
 * @param h
 * @return
 */
ret_code_t hal_gpio_port_close(hal_gpio_t *h) {
    /* 静态句柄方案：关闭可做 no-op */
    (void) h;
    return RET_OK;
}

/* ---------------- 热函数：不返回状态码 ---------------- */
/**
 * @brief 往指定的GPIO输出指定电平
 * @param h GPIO
 * @param level 电平
 */
void hal_gpio_port_write(hal_gpio_t *h, hal_gpio_level_t level) {
    HAL_GPIO_ASSERT(h != NULL);
    HAL_GPIO_ASSERT(h->port != NULL);
    HAL_GPIO_ASSERT(h->pin < 16u);
    HAL_GPIO_ASSERT(level < HAL_GPIO_LEVEL_MAX);

    HAL_GPIO_WritePin(h->port, pin_mask(h->pin), (GPIO_PinState) level);
}

/**
 * @brief 读取指定的GPIO电平
 * @param h
 * @return
 */
hal_gpio_level_t hal_gpio_port_read(hal_gpio_t *h) {
    HAL_GPIO_ASSERT(h != NULL);
    HAL_GPIO_ASSERT(h->port != NULL);
    HAL_GPIO_ASSERT(h->pin < 16u);

    return (hal_gpio_level_t) HAL_GPIO_ReadPin(h->port, pin_mask(h->pin));
}

/**
 * @brief 翻转指定GPIO的输出电平
 * @param h
 */
void hal_gpio_port_toggle(hal_gpio_t *h) {
    HAL_GPIO_ASSERT(h != NULL);
    HAL_GPIO_ASSERT(h->port != NULL);
    HAL_GPIO_ASSERT(h->pin < 16u);

    HAL_GPIO_TogglePin(h->port, pin_mask(h->pin));
}
