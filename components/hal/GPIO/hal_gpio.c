#include "hal_gpio.h"

#include "APP_config.h"

#if (defined(CFG_FEAT_HAL_GPIO) && (CFG_FEAT_HAL_GPIO == 1))

/**
 * 平台接口
 */
ret_code_t hal_gpio_port_open(hal_gpio_t** out, uint32_t id);

ret_code_t hal_gpio_port_config(hal_gpio_t* h, const hal_gpio_cfg_t* cfg);

ret_code_t hal_gpio_port_close(const hal_gpio_t* h);

void hal_gpio_port_write(const hal_gpio_t* h, hal_gpio_level_t level);

hal_gpio_level_t hal_gpio_port_read(const hal_gpio_t* h);

void hal_gpio_port_toggle(const hal_gpio_t* h);

ret_code_t hal_gpio_port_register_irq(hal_gpio_t* h, hal_gpio_irq_cb_t cb, void* user_data);

ret_code_t hal_gpio_port_unregister_irq(hal_gpio_t* h);

/**
 * @brief 从port实现函数返回映射的指定GPIO port与PIN结构体
 * @param out 接收结构体的地址
 * @param id  全局引脚id
 * @return
 */
ret_code_t hal_gpio_open(hal_gpio_t** out, uint32_t id) {
    return hal_gpio_port_open(out, id);
}

/**
 * @brief 初始化指定的GPIO
 * @param h GPIO
 * @param cfg 配置结构体
 * @return 运行状态
 */
ret_code_t hal_gpio_config(hal_gpio_t* h, const hal_gpio_cfg_t* cfg) {
    return hal_gpio_port_config(h, cfg);
}

/**
 * @brief 关闭GPIO
 * @param h
 * @return
 */
ret_code_t hal_gpio_close(hal_gpio_t* h) {
    return hal_gpio_port_close(h);
}

/**
 * @brief 往指定的GPIO输出指定电平
 * @param h GPIO
 * @param level 电平
 */
void hal_gpio_write(hal_gpio_t* h, hal_gpio_level_t level) {
    hal_gpio_port_write(h, level);
}

/**
 * @brief 读取指定的GPIO电平
 * @param h
 * @return
 */
hal_gpio_level_t hal_gpio_read(hal_gpio_t* h) {
    return hal_gpio_port_read(h);
}

/**
 * @brief 翻转指定GPIO的输出电平
 * @param h
 */
void hal_gpio_toggle(hal_gpio_t* h) {
    hal_gpio_port_toggle(h);
}

/**
 * @brief  注册 GPIO 中断回调
 */
ret_code_t hal_gpio_register_irq(hal_gpio_t* h, hal_gpio_irq_cb_t cb, void* user_data) {
    return hal_gpio_port_register_irq(h, cb, user_data);
}

/**
 * @brief  注销 GPIO 中断回调
 */
ret_code_t hal_gpio_unregister_irq(hal_gpio_t* h) {
    return hal_gpio_port_unregister_irq(h);
}
#else
ret_code_t hal_gpio_open(hal_gpio_t** out, uint32_t id) {
    (void)out;
    (void)id;
    return RET_E_UNSUPPORTED;
}

ret_code_t hal_gpio_config(hal_gpio_t* h, const hal_gpio_cfg_t* cfg) {
    (void)h;
    (void)cfg;
    return RET_E_UNSUPPORTED;
}

ret_code_t hal_gpio_close(hal_gpio_t* h) {
    (void)h;
    return RET_E_UNSUPPORTED;
}

void hal_gpio_write(hal_gpio_t* h, hal_gpio_level_t level) {
    (void)h;
    (void)level;
}

hal_gpio_level_t hal_gpio_read(hal_gpio_t* h) {
    (void)h;
    return HAL_GPIO_LEVEL_LOW;
}

void hal_gpio_toggle(hal_gpio_t* h) {
    (void)h;
}

ret_code_t hal_gpio_register_irq(hal_gpio_t* h, hal_gpio_irq_cb_t cb, void* user_data) {
    (void)h;
    (void)cb;
    (void)user_data;
    return RET_E_UNSUPPORTED;
}

ret_code_t hal_gpio_unregister_irq(hal_gpio_t* h) {
    (void)h;
    return RET_E_UNSUPPORTED;
}
#endif


