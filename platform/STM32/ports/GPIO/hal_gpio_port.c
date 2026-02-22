#include <stm32f4xx_hal.h> /* 确保包含完整 HAL 定义 (IRQn_Type 等) */

#include "APP_config.h"
#include "stm32_hal_config.h"

/* hal抽象选择宏 */
#if (defined(CFG_TARGET_PLATFORM_STM32_HAL) && (CFG_TARGET_PLATFORM_STM32_HAL == 1)) && (defined(CFG_FEAT_HAL_GPIO) && (CFG_FEAT_HAL_GPIO == 1))
#include <stdint.h>
#include <string.h>

#include "board_gpio_map.h"
#include "compiler_cus.h"
#include "hal_gpio.h"
#include "ret_code.h"
#include "stm32_hal.h"
#define PORT_RET(clas_, err_) \
    RET_MAKE(RET_MOD_PORT, RET_SUB_PORT_STM32, RET_CODE_MAKE((clas_), (err_)))
/* ---------------- 断言（热路径用） ---------------- */

__WEAK NORETURN void hal_gpio_assert_failed(const char* file, int line) {
    (void)file;
    (void)line;
    __disable_irq();
    while (1) {
        /* fail-stop */
    }
}

#ifndef HAL_GPIO_ASSERT
#ifdef DEBUG_MODE
#define HAL_GPIO_ASSERT(x)                                    \
    do {                                                      \
        if (!(x)) hal_gpio_assert_failed(__FILE__, __LINE__); \
    } while (0)
#else
#define HAL_GPIO_ASSERT(x)
#endif
#endif

#ifndef HAL_GPIO_IRQ_PRIO
#define HAL_GPIO_IRQ_PRIO 6u
#endif

#ifndef HAL_GPIO_IRQ_SUBPRIO
#define HAL_GPIO_IRQ_SUBPRIO 0u
#endif

/* ---------------- 句柄定义（只在 port.c 可见） ---------------- */

struct hal_gpio {
    GPIO_TypeDef* port;
    uint16_t pin; /* 0..15 */
    uint32_t id;
};

/* ---------------- 内部工具 ---------------- */

static inline uint16_t pin_mask(uint16_t pin) {
    return (uint16_t)(1u << pin);
}

/**
 * @brief 开启对应的时钟
 * @param GPIOx GPIOx
 * @return 时钟开启结果
 */
static ret_code_t gpio_enable_clock(const GPIO_TypeDef* GPIOx) {
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
    return PORT_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
}

/**
 * @brief 上下拉枚举映射
 * @param p
 * @param out
 * @return 被映射的平台上下拉枚举
 */
static ret_code_t map_pull(hal_gpio_pull_t p, uint32_t* out) {
    // ReSharper disable once CppDFAConstantConditions
    if (!out) return PORT_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    switch (p) {
        case HAL_GPIO_PULL_NONE:
            *out = GPIO_NOPULL;
            return RET_OK;
        case HAL_GPIO_PULL_UP:
            *out = GPIO_PULLUP;
            return RET_OK;
        case HAL_GPIO_PULL_DOWN:
            *out = GPIO_PULLDOWN;
            return RET_OK;
        default:
            return PORT_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    }
}

/**
 * @brief 速度枚举映射
 * @param s
 * @param out
 * @return
 */
static ret_code_t map_speed(hal_gpio_speed_t s, uint32_t* out) {
    // ReSharper disable once CppDFAConstantConditions
    if (!out) return PORT_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    switch (s) {
        case HAL_GPIO_SPEED_LOW:
            *out = GPIO_SPEED_FREQ_LOW;
            return RET_OK;
        case HAL_GPIO_SPEED_MEDIUM:
            *out = GPIO_SPEED_FREQ_MEDIUM;
            return RET_OK;
        case HAL_GPIO_SPEED_HIGH:
            *out = GPIO_SPEED_FREQ_HIGH;
            return RET_OK;
        case HAL_GPIO_SPEED_VERY_HIGH:
            *out = GPIO_SPEED_FREQ_VERY_HIGH;
            return RET_OK;
        default:
            return PORT_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    }
}

/**
 * @brief 复用功能映射
 * @param in_af
 * @param out_af
 * @return ret_code_t
 */
static ret_code_t map_alternate(uint32_t in_af, uint32_t* out_af) {
    // ReSharper disable once CppDFAConstantConditions
    if (!out_af) return PORT_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);

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
    return PORT_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
}

/* ---------------- 平台导出：供 components/hal/src/hal_gpio.c 调用 ---------------- */
/**
 * @brief 从板级GPIO映射表获取 具体的GPIO
 * @param out 存储具体的port/Pin
 * @param id 板级映射
 * @return 返回状态码
 * @note
 */
ret_code_t hal_gpio_port_open(hal_gpio_t** out, uint32_t id) {
    if (!out) return PORT_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);

    board_gpio_hw_t hw;
    /* 返回 port & Pin */
    const ret_code_t rc = board_gpio_lookup(id, &hw);
    if (rc != RET_OK) return rc;
    if (!hw.port || hw.pin >= 16u) return PORT_RET(RET_CLASS_STATE, RET_R_NOT_READY);

    static hal_gpio_t handles[BOARD_GPIO_MAP_MAX]; /* 简化静态池*/
    static uint8_t used[BOARD_GPIO_MAP_MAX] = {0}; /* 标记使用过的GPIO */

    /* 简化策略：id 必须 < BOARD_GPIO_MAP_MAX，直接映射到句柄槽位 */
    /*　如果在静态池中找到了　*/
    for (uint32_t i = 0; i < (uint32_t)(sizeof(handles) / sizeof(handles[0])); ++i) {
        if (used[i] && handles[i].id == id) {
            *out = &handles[i];
            return RET_OK;
        }
    }

    /* 静态池中没有，将获取到的 rc 和 id 绑定添加进去 */
    for (uint32_t i = 0; i < (uint32_t)(sizeof(handles) / sizeof(handles[0])); ++i) {
        if (!used[i]) {
            handles[i].id   = id;
            handles[i].port = hw.port;
            handles[i].pin  = hw.pin;
            used[i]         = 1;
            *out            = &handles[i];
            return RET_OK;
        }
    }

    return PORT_RET(RET_CLASS_RESOURCE, RET_R_NO_MEM);
}

/**
 * @brief 初始化指定的GPIO
 * @param h GPIO
 * @param cfg 配置结构体
 * @return 运行状态
 */
ret_code_t hal_gpio_port_config(hal_gpio_t* h, const hal_gpio_cfg_t* cfg) {
    /* 检查非空指针 */
    if (!h || !cfg) return PORT_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    /* 检查合法参数 */
    if (h->pin >= 16u) return PORT_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    if (cfg->dir >= HAL_GPIO_DIR_MAX || cfg->pull >= HAL_GPIO_PULL_MAX ||
        cfg->speed >= HAL_GPIO_SPEED_MAX || cfg->irq >= HAL_GPIO_IRQ_MAX ||
        cfg->out_type >= HAL_GPIO_OUT_MAX || cfg->default_level >= HAL_GPIO_LEVEL_MAX) {
        return PORT_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
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
    GPIO_InitTypeDef init = {0};

    /* 填充引脚、上下拉、速度 */
    init.Pin              = pin_mask(h->pin);
    init.Pull             = pull;
    init.Speed            = speed;

    /* mode 映射：由通用 cfg 映射到 STM32 HAL */
    if (cfg->irq != HAL_GPIO_IRQ_NONE) {
        /* EXTI 触发模式（NVIC 不在这里配置） */
        if (cfg->irq == HAL_GPIO_IRQ_RISING)
            init.Mode = GPIO_MODE_IT_RISING;
        else if (cfg->irq == HAL_GPIO_IRQ_FALLING)
            init.Mode = GPIO_MODE_IT_FALLING;
        else
            init.Mode = GPIO_MODE_IT_RISING_FALLING;
    } else if (cfg->dir == HAL_GPIO_DIR_IN) {
        init.Mode = GPIO_MODE_INPUT;
    } else {
        /* 输出模式：推挽/开漏 */
        init.Mode = (cfg->out_type == HAL_GPIO_OUT_OD) ? GPIO_MODE_OUTPUT_OD : GPIO_MODE_OUTPUT_PP;
    }

    /* AF：只有在你定义为复用的场景才需要，这里用：alternate!=0xFFFFFFFF 作为“启用AF”的开关 */
    if (cfg->alternate != HAL_GPIO_AF_NONE) {
        uint32_t af = 0;
        rc          = map_alternate(cfg->alternate, &af);
        if (rc != RET_OK) return rc;

        init.Mode      = (cfg->out_type == HAL_GPIO_OUT_OD) ? GPIO_MODE_AF_OD : GPIO_MODE_AF_PP;
        init.Alternate = af;
    }

    HAL_GPIO_Init(h->port, &init);

    /* default_level：仅对“纯输出”生效（AF/中断/输入不写） */
    if (cfg->irq == HAL_GPIO_IRQ_NONE && cfg->dir == HAL_GPIO_DIR_OUT &&
        cfg->alternate == HAL_GPIO_AF_NONE) {
        HAL_GPIO_WritePin(h->port, pin_mask(h->pin), (GPIO_PinState)cfg->default_level);
    }

    return RET_OK;
}

/**
 * @brief 关闭GPIO
 * @param h
 * @return
 */
ret_code_t hal_gpio_port_close(const hal_gpio_t* h) {
    /* 静态句柄方案：关闭可做 no-op */
    (void)h;
    return RET_OK;
}

/* ---------------- 热函数：不返回状态码 ---------------- */
/**
 * @brief 往指定的GPIO输出指定电平
 * @param h GPIO
 * @param level 电平
 */
void hal_gpio_port_write(const hal_gpio_t* h, hal_gpio_level_t level) {
    HAL_GPIO_ASSERT(h != NULL);
    HAL_GPIO_ASSERT(h->port != NULL);
    HAL_GPIO_ASSERT(h->pin < 16u);
    HAL_GPIO_ASSERT(level < HAL_GPIO_LEVEL_MAX);

    HAL_GPIO_WritePin(h->port, pin_mask(h->pin), (GPIO_PinState)level);
}

/**
 * @brief 读取指定的GPIO电平
 * @param h
 * @return
 */
hal_gpio_level_t hal_gpio_port_read(const hal_gpio_t* h) {
    HAL_GPIO_ASSERT(h != NULL);
    HAL_GPIO_ASSERT(h->port != NULL);
    HAL_GPIO_ASSERT(h->pin < 16u);

    return (hal_gpio_level_t)HAL_GPIO_ReadPin(h->port, pin_mask(h->pin));
}

/**
 * @brief 翻转指定GPIO的输出电平
 * @param h
 */
void hal_gpio_port_toggle(const hal_gpio_t* h) {
    HAL_GPIO_ASSERT(h != NULL);
    HAL_GPIO_ASSERT(h->port != NULL);
    HAL_GPIO_ASSERT(h->pin < 16u);

    HAL_GPIO_TogglePin(h->port, pin_mask(h->pin));
}

/**============================================================================================ */
/**==================================       中断回调       ===================================== */
/**============================================================================================ */

typedef struct {
    hal_gpio_irq_cb_t cb;
    void* user_data;
} gpio_irq_entry_t;

/* EXTI0..15 对应 16 个引脚号 */
static gpio_irq_entry_t s_gpio_irq_cbs[16];

/**
 * @brief  获取引脚对应的 IRQn
 */
static IRQn_Type get_pin_irqn(uint16_t pin) {
    if (pin == 0) return EXTI0_IRQn;
    if (pin == 1) return EXTI1_IRQn;
    if (pin == 2) return EXTI2_IRQn;
    if (pin == 3) return EXTI3_IRQn;
    if (pin == 4) return EXTI4_IRQn;
    if (pin >= 5 && pin <= 9) return EXTI9_5_IRQn;
    if (pin >= 10 && pin <= 15) return EXTI15_10_IRQn;
    return NonMaskableInt_IRQn; /* Should not happen */
}
/**
 * @brief 判断当前的中断和pin是否是对上的
 * @param pin pin
 * @param irqn 中断
 * @return 是否是配对的
 */
static bool pin_belongs_to_irqn(uint16_t pin, IRQn_Type irqn) {
    switch (irqn) {
        case EXTI0_IRQn:
            return pin == 0u;
        case EXTI1_IRQn:
            return pin == 1u;
        case EXTI2_IRQn:
            return pin == 2u;
        case EXTI3_IRQn:
            return pin == 3u;
        case EXTI4_IRQn:
            return pin == 4u;
        case EXTI9_5_IRQn:
            return (pin >= 5u && pin <= 9u);
        case EXTI15_10_IRQn:
            return (pin >= 10u && pin <= 15u);
        default:
            return false;
    }
}
/**
 * @brief
 * @param irqn 中断
 * @return
 */
static bool any_line_uses_irqn(IRQn_Type irqn) {
    for (uint16_t pin = 0; pin < 16u; ++pin) {
        if (pin_belongs_to_irqn(pin, irqn) && (s_gpio_irq_cbs[pin].cb != NULL)) return true;
    }
    return false;
}

/**
 * @brief 传入配置信息注册回调函数
 * @param h GPIO信息
 * @param cb 回调函数
 * @param user_data 上下文信息
 * @return
 */
ret_code_t hal_gpio_port_register_irq(hal_gpio_t* h, hal_gpio_irq_cb_t cb, void* user_data) {
    if (!h || !cb) return PORT_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    if (h->pin >= 16u) return PORT_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);

    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    /*　判断当前位置是否已经注册了一个回调函数　*/
    if (s_gpio_irq_cbs[h->pin].cb && s_gpio_irq_cbs[h->pin].cb != cb) {
        __set_PRIMASK(primask);
        return PORT_RET(RET_CLASS_STATE, RET_R_BUSY);
    }

    /* 1. 存入回调表 */
    s_gpio_irq_cbs[h->pin].cb        = cb;
    s_gpio_irq_cbs[h->pin].user_data = user_data;
    __set_PRIMASK(primask);

    /* 2. 使能 NVIC */
    const IRQn_Type irqn = get_pin_irqn(h->pin);
    __HAL_GPIO_EXTI_CLEAR_IT(pin_mask(h->pin));
    HAL_NVIC_SetPriority(irqn, HAL_GPIO_IRQ_PRIO, HAL_GPIO_IRQ_SUBPRIO);
    HAL_NVIC_EnableIRQ(irqn);

    return RET_OK;
}

/**
 * @brief 注销指定GPIO的中断
 * @param h GPIO信息
 * @return 32位状态码
 * @note  暂未实现 Disable IRQ
 */
ret_code_t hal_gpio_port_unregister_irq(hal_gpio_t* h) {
    if (!h) return PORT_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    if (h->pin >= 16u) return PORT_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    /* 获取中断 */
    const IRQn_Type irqn   = get_pin_irqn(h->pin);
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    /* 注销回调 */
    s_gpio_irq_cbs[h->pin].cb        = NULL;
    s_gpio_irq_cbs[h->pin].user_data = NULL;
    const bool irqn_in_use           = any_line_uses_irqn(irqn);
    __set_PRIMASK(primask);
    /* 该中断无任何引用后关闭 */
    if (!irqn_in_use) HAL_NVIC_DisableIRQ(irqn);
    return RET_OK;
}

/**
 * @brief  STM32 HAL GPIO EXTI 回调
 *         所有 EXTI 中断最终都会调到这里
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    /* GPIO_Pin 是 bit mask (e.g. 0x0001, 0x0002...) */
    for (uint16_t i = 0; i < 16; ++i) {
        if (GPIO_Pin & (1u << i)) {
            if (s_gpio_irq_cbs[i].cb) {
                s_gpio_irq_cbs[i].cb(s_gpio_irq_cbs[i].user_data);
            }
        }
    }
}

/* ---------------- EXTI 中断服务程序 ---------------- */
/*
 * 必须实现所有潜在的 EXTI Handler，否则链接时可能找不到，
 * 或者如果 startup 文件定义了 weak symbol，则不会进入我们的逻辑。
 * 此处显式实现以确保 HAL_GPIO_EXTI_IRQHandler 被调用。
 */

void EXTI0_IRQHandler(void) {
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0);
}

void EXTI1_IRQHandler(void) {
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_1);
}

void EXTI2_IRQHandler(void) {
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_2);
}

void EXTI3_IRQHandler(void) {
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_3);
}

void EXTI4_IRQHandler(void) {
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_4);
}

void EXTI9_5_IRQHandler(void) {
    /* 5..9 */
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_5)) HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_5);
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_6)) HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_6);
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_7)) HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_7);
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_8)) HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_8);
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_9)) HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_9);
}

void EXTI15_10_IRQHandler(void) {
    /* 10..15 */
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_10)) HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_10);
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_11)) HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_11);
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_12)) HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_12);
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_13)) HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_13);
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_14)) HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_14);
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_15)) HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_15);
}

#endif


