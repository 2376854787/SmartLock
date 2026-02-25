#include "hal_gpio.h"

#include "APP_config.h"
#include "assert_cus.h"
#include "osal.h"
#if defined(CFG_FEAT_LOG_SYSTEM) && (CFG_FEAT_LOG_SYSTEM == 1)
#include "log.h"
#endif

#define GPIO_HAL_PARAM(reason_)   RET_MAKE_PARAM(RET_MOD_HAL, RET_SUB_HAL_GPIO, (reason_))
#define GPIO_HAL_STATE(reason_)   RET_MAKE_STATE(RET_MOD_HAL, RET_SUB_HAL_GPIO, (reason_))
#define GPIO_HAL_TIMEOUT(reason_) RET_MAKE_TIMEOUT(RET_MOD_HAL, RET_SUB_HAL_GPIO, (reason_))
#define GPIO_HAL_IO(reason_)      RET_MAKE_IO(RET_MOD_HAL, RET_SUB_HAL_GPIO, (reason_))
#define GPIO_HAL_RES(reason_)     RET_MAKE_RESOURCE(RET_MOD_HAL, RET_SUB_HAL_GPIO, (reason_))

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

__attribute__((weak)) void hal_gpio_on_port_error(ret_code_t rc_port, ret_code_t rc_hal,
                                                  const char* api, uint32_t arg0,
                                                  uint32_t arg1) {
#if defined(CFG_FEAT_LOG_SYSTEM) && (CFG_FEAT_LOG_SYSTEM == 1) && \
    defined(CFG_PARAM_GPIO_LOG_PORT_ERR) && (CFG_PARAM_GPIO_LOG_PORT_ERR == 1)
#if defined(CFG_PARAM_GPIO_LOG_PORT_ERR_IN_ISR) && (CFG_PARAM_GPIO_LOG_PORT_ERR_IN_ISR == 1)
    if (ret_is_err(rc_port)) {
#else
    if (ret_is_err(rc_port) && !OSAL_in_isr()) {
#endif
        LOG_E("HAL_GPIO", "api:%s port:0x%08lX->hal:0x%08lX arg0:%lu arg1:%lu",
              (api != NULL) ? api : "unknown", (unsigned long)rc_port, (unsigned long)rc_hal,
              (unsigned long)arg0, (unsigned long)arg1);
    }
#else
    (void)rc_port;
    (void)rc_hal;
    (void)api;
    (void)arg0;
    (void)arg1;
#endif
}

static inline ret_code_t gpio_map_port_to_hal(ret_code_t rc_port, const char* api, uint32_t arg0,
                                              uint32_t arg1) {
    if (ret_is_ok(rc_port)) return RET_OK;

    ret_code_t rc_hal = GPIO_HAL_IO(RET_R_IO);

    if (ret_is_class(rc_port, RET_CLASS_PARAM)) {
        if (ret_is_reason(rc_port, RET_R_NULL_PTR))
            rc_hal = GPIO_HAL_PARAM(RET_R_NULL_PTR);
        else if (ret_is_reason(rc_port, RET_R_RANGE_ERR))
            rc_hal = GPIO_HAL_PARAM(RET_R_RANGE_ERR);
        else if (ret_is_reason(rc_port, RET_R_UNSUPPORTED))
            rc_hal = GPIO_HAL_PARAM(RET_R_UNSUPPORTED);
        else
            rc_hal = GPIO_HAL_PARAM(RET_R_INVALID_ARG);
    } else if (ret_is_class(rc_port, RET_CLASS_TIMEOUT)) {
        rc_hal = GPIO_HAL_TIMEOUT(RET_R_TIMEOUT);
    } else if (ret_is_class(rc_port, RET_CLASS_RESOURCE)) {
        if (ret_is_reason(rc_port, RET_R_NO_MEM))
            rc_hal = GPIO_HAL_RES(RET_R_NO_MEM);
        else
            rc_hal = GPIO_HAL_RES(RET_R_NO_RESOURCE);
    } else if (ret_is_class(rc_port, RET_CLASS_STATE)) {
        if (ret_is_reason(rc_port, RET_R_BUSY))
            rc_hal = GPIO_HAL_STATE(RET_R_BUSY);
        else if (ret_is_reason(rc_port, RET_R_NOT_READY))
            rc_hal = GPIO_HAL_STATE(RET_R_NOT_READY);
        else
            rc_hal = GPIO_HAL_STATE(RET_R_STATE_ERR);
    }

    hal_gpio_on_port_error(rc_port, rc_hal, api, arg0, arg1);
    return rc_hal;
}

/**
 * @brief 从port实现函数返回映射的指定GPIO port与PIN结构体
 * @param out 接收结构体的地址
 * @param id  全局引脚id
 * @return
 */
ret_code_t hal_gpio_open(hal_gpio_t** out, uint32_t id) {
    ASSERT_PARAM(out != NULL);
    if (out != NULL) *out = NULL;
    REQUIRE_RET(out != NULL, GPIO_HAL_PARAM(RET_R_NULL_PTR));
    const ret_code_t rc = hal_gpio_port_open(out, id);
    if (ret_is_err(rc)) return gpio_map_port_to_hal(rc, "hal_gpio_open", id, 0u);
    return RET_OK;
}

/**
 * @brief 初始化指定的GPIO
 * @param h GPIO句柄
 * @param cfg 配置结构体
 * @return 32位状态码
 */
ret_code_t hal_gpio_config(hal_gpio_t* h, const hal_gpio_cfg_t* cfg) {
    ASSERT_PARAM((h != NULL) && (cfg != NULL));
    REQUIRE_RET((h != NULL) && (cfg != NULL), GPIO_HAL_PARAM(RET_R_INVALID_ARG));
    const ret_code_t rc = hal_gpio_port_config(h, cfg);
    if (ret_is_err(rc)) return gpio_map_port_to_hal(rc, "hal_gpio_config", 0u, 0u);
    return RET_OK;
}

/**
 * @brief 关闭GPIO
 * @param h 句柄
 * @return 32位状态码
 */
ret_code_t hal_gpio_close(hal_gpio_t* h) {
    ASSERT_PARAM(h != NULL);
    REQUIRE_RET(h != NULL, GPIO_HAL_PARAM(RET_R_INVALID_ARG));
    const ret_code_t rc = hal_gpio_port_close(h);
    if (ret_is_err(rc)) return gpio_map_port_to_hal(rc, "hal_gpio_close", 0u, 0u);
    return RET_OK;
}

/**
 * @brief 往指定的GPIO输出指定电平
 * @param h GPIO句柄
 * @param level 电平
 */
void hal_gpio_write(hal_gpio_t* h, hal_gpio_level_t level) {
    ASSERT_PARAM(h != NULL);
    REQUIRE_RET_VOID(h != NULL);
    hal_gpio_port_write(h, level);
}

/**
 * @brief 读取指定的GPIO电平
 * @param h
 * @return
 */
hal_gpio_level_t hal_gpio_read(hal_gpio_t* h) {
    ASSERT_PARAM(h != NULL);
    REQUIRE_RET(h != NULL, HAL_GPIO_LEVEL_LOW);
    return hal_gpio_port_read(h);
}

/**
 * @brief 翻转指定GPIO的输出电平
 * @param h GPIO句柄
 */
void hal_gpio_toggle(hal_gpio_t* h) {
    ASSERT_PARAM(h != NULL);
    REQUIRE_RET_VOID(h != NULL);
    hal_gpio_port_toggle(h);
}

/**
 * @brief  注册 GPIO 中断回调
 */
ret_code_t hal_gpio_register_irq(hal_gpio_t* h, hal_gpio_irq_cb_t cb, void* user_data) {
    ASSERT_PARAM((h != NULL) && (cb != NULL));
    REQUIRE_RET((h != NULL) && (cb != NULL), GPIO_HAL_PARAM(RET_R_INVALID_ARG));
    const ret_code_t rc = hal_gpio_port_register_irq(h, cb, user_data);
    if (ret_is_err(rc)) return gpio_map_port_to_hal(rc, "hal_gpio_register_irq", 0u, 0u);
    return RET_OK;
}

/**
 * @brief  注销 GPIO 中断回调
 */
ret_code_t hal_gpio_unregister_irq(hal_gpio_t* h) {
    ASSERT_PARAM(h != NULL);
    REQUIRE_RET(h != NULL, GPIO_HAL_PARAM(RET_R_INVALID_ARG));
    const ret_code_t rc = hal_gpio_port_unregister_irq(h);
    if (ret_is_err(rc)) return gpio_map_port_to_hal(rc, "hal_gpio_unregister_irq", 0u, 0u);
    return RET_OK;
}
#else
ret_code_t hal_gpio_open(hal_gpio_t** out, uint32_t id) {
    (void)out;
    (void)id;
    return GPIO_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_gpio_config(hal_gpio_t* h, const hal_gpio_cfg_t* cfg) {
    (void)h;
    (void)cfg;
    return GPIO_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_gpio_close(hal_gpio_t* h) {
    (void)h;
    return GPIO_HAL_PARAM(RET_R_UNSUPPORTED);
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
    return GPIO_HAL_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t hal_gpio_unregister_irq(hal_gpio_t* h) {
    (void)h;
    return GPIO_HAL_PARAM(RET_R_UNSUPPORTED);
}
#endif
