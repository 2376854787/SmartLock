#include <assert.h>
#include <stdio.h>

#include "hal_gpio.h"

struct hal_gpio {
    uint32_t id;
    hal_gpio_level_t level;
    hal_gpio_irq_cb_t irq_cb;
    void *irq_user;
    bool configured;
};

static struct hal_gpio g_gpio = {0};
static int g_irq_calls        = 0;

ret_code_t hal_gpio_port_acquire(hal_gpio_t **out, uint32_t id) {
    if ((out == NULL) || (id > 3u)) return RET_MAKE_PARAM(RET_MOD_PORT, RET_SUB_PORT_GPIO, RET_R_INVALID_ARG);
    g_gpio.id         = id;
    g_gpio.level      = HAL_GPIO_LEVEL_LOW;
    g_gpio.irq_cb     = NULL;
    g_gpio.irq_user   = NULL;
    g_gpio.configured = false;
    *out              = &g_gpio;
    return RET_OK;
}

ret_code_t hal_gpio_port_config(hal_gpio_t *h, const hal_gpio_cfg_t *cfg) {
    if ((h == NULL) || (cfg == NULL)) return RET_MAKE_PARAM(RET_MOD_PORT, RET_SUB_PORT_GPIO, RET_R_INVALID_ARG);
    h->configured = true;
    h->level      = cfg->default_level;
    return RET_OK;
}

ret_code_t hal_gpio_port_release(const hal_gpio_t *h) {
    return (h != NULL) ? RET_OK : RET_MAKE_PARAM(RET_MOD_PORT, RET_SUB_PORT_GPIO, RET_R_INVALID_ARG);
}

void hal_gpio_port_write(const hal_gpio_t *h, hal_gpio_level_t level) {
    ((struct hal_gpio *)h)->level = level;
}

hal_gpio_level_t hal_gpio_port_read(const hal_gpio_t *h) {
    return (h != NULL) ? ((const struct hal_gpio *)h)->level : HAL_GPIO_LEVEL_LOW;
}

void hal_gpio_port_toggle(const hal_gpio_t *h) {
    struct hal_gpio *gpio = (struct hal_gpio *)h;
    gpio->level = (gpio->level == HAL_GPIO_LEVEL_LOW) ? HAL_GPIO_LEVEL_HIGH : HAL_GPIO_LEVEL_LOW;
}

ret_code_t hal_gpio_port_register_irq(hal_gpio_t *h, hal_gpio_irq_cb_t cb, void *user_data) {
    if ((h == NULL) || (cb == NULL)) return RET_MAKE_PARAM(RET_MOD_PORT, RET_SUB_PORT_GPIO, RET_R_INVALID_ARG);
    h->irq_cb   = cb;
    h->irq_user = user_data;
    return RET_OK;
}

ret_code_t hal_gpio_port_unregister_irq(hal_gpio_t *h) {
    if (h == NULL) return RET_MAKE_PARAM(RET_MOD_PORT, RET_SUB_PORT_GPIO, RET_R_INVALID_ARG);
    h->irq_cb   = NULL;
    h->irq_user = NULL;
    return RET_OK;
}

static void test_irq_cb(void *user) {
    assert(user == &g_gpio);
    ++g_irq_calls;
}

int main(void) {
    hal_gpio_t *gpio = NULL;
    const hal_gpio_cfg_t cfg = {
        .dir = HAL_GPIO_DIR_OUT,
        .out_type = HAL_GPIO_OUT_PP,
        .pull = HAL_GPIO_PULL_NONE,
        .speed = HAL_GPIO_SPEED_LOW,
        .irq = HAL_GPIO_IRQ_NONE,
        .alternate = HAL_GPIO_AF_NONE,
        .default_level = HAL_GPIO_LEVEL_HIGH,
    };

    assert(ret_is_ok(hal_gpio_acquire(&gpio, 1u)));
    assert(gpio == &g_gpio);
    assert(ret_is_ok(hal_gpio_config(gpio, &cfg)));
    assert(hal_gpio_read(gpio) == HAL_GPIO_LEVEL_HIGH);

    hal_gpio_write(gpio, HAL_GPIO_LEVEL_LOW);
    assert(hal_gpio_read(gpio) == HAL_GPIO_LEVEL_LOW);
    hal_gpio_toggle(gpio);
    assert(hal_gpio_read(gpio) == HAL_GPIO_LEVEL_HIGH);

    assert(ret_is_ok(hal_gpio_register_irq(gpio, test_irq_cb, &g_gpio)));
    g_gpio.irq_cb(g_gpio.irq_user);
    assert(g_irq_calls == 1);
    assert(ret_is_ok(hal_gpio_unregister_irq(gpio)));
    assert(ret_is_ok(hal_gpio_release(gpio)));

    puts("test_hal_gpio: PASS");
    return 0;
}
