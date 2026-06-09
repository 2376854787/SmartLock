#ifndef HAL_GPIO_H
#define HAL_GPIO_H
#include <stdbool.h>
#include <stdint.h>

#include "ret_code_t.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct hal_gpio hal_gpio_t;  /** 不透明句柄 */
#define HAL_GPIO_AF_NONE 0xFFFFFFFFu /** 不开启 AF功能 */

/** 电平 **/
typedef enum {
    HAL_GPIO_LEVEL_LOW  = 0,
    HAL_GPIO_LEVEL_HIGH = 1,
    HAL_GPIO_LEVEL_MAX
} hal_gpio_level_t;

/** 方向 **/
typedef enum { HAL_GPIO_DIR_IN = 0, HAL_GPIO_DIR_OUT, HAL_GPIO_DIR_MAX } hal_gpio_dir_t;

/** 上拉下拉 **/
typedef enum {
    HAL_GPIO_PULL_NONE = 0,
    HAL_GPIO_PULL_UP,
    HAL_GPIO_PULL_DOWN,
    HAL_GPIO_PULL_MAX
} hal_gpio_pull_t;

/** 输出类型 **/
typedef enum { HAL_GPIO_OUT_PP = 0, HAL_GPIO_OUT_OD, HAL_GPIO_OUT_MAX } hal_gpio_out_t;

/** 速度 **/
typedef enum {
    HAL_GPIO_SPEED_LOW = 0,
    HAL_GPIO_SPEED_MEDIUM,
    HAL_GPIO_SPEED_HIGH,
    HAL_GPIO_SPEED_VERY_HIGH,
    HAL_GPIO_SPEED_MAX
} hal_gpio_speed_t;

/** 中断触发 **/
typedef enum {
    HAL_GPIO_IRQ_NONE = 0,
    HAL_GPIO_IRQ_RISING,
    HAL_GPIO_IRQ_FALLING,
    HAL_GPIO_IRQ_BOTH,
    HAL_GPIO_IRQ_MAX
} hal_gpio_irq_t;

/** GPIO 配置 */
typedef struct {
    hal_gpio_dir_t dir;             /**< 输入/输出 **/
    hal_gpio_out_t out_type;        /**< 推挽/开漏 **/
    hal_gpio_pull_t pull;           /**< 上下拉 **/
    hal_gpio_speed_t speed;         /**< 速度 */
    hal_gpio_irq_t irq;             /**< 中断触发方式 **/
    uint32_t alternate;             /**< 复用 AF（可选：0..15 或平台 AF 宏，由 port 层解释） */
    hal_gpio_level_t default_level; /**< 输出默认电平（仅输出） */
} hal_gpio_cfg_t;

/**
 * @brief 通过 board id 获取 GPIO 句柄
 * @param out   输出句柄
 * @param id    板级编号（由 board_gpio_map.c 定义）
 * @note 该接口只做句柄映射，不执行硬件配置；硬件配置请调用 hal_gpio_config
 */
ret_code_t hal_gpio_acquire(hal_gpio_t** out, uint32_t id);

/**
 * @brief 配置 GPIO（低频路径：返回状态码）
 * @note  若 irq != NONE：本函数仅配置 EXTI 触发方式；NVIC 由 board 层配置（商业常见分层）
 */
ret_code_t hal_gpio_config(hal_gpio_t* h, const hal_gpio_cfg_t* cfg);

/**
 * @brief 释放 GPIO 句柄（对静态句柄映射实现可为 no-op）
 */
ret_code_t hal_gpio_release(hal_gpio_t* h);

/* ---------------- 热路径 API：不返回状态码 ---------------- */

/**
 * @brief 写电平
 * @note  参数错误属于编程错误：Debug 断言；Release 可不检查
 */
void hal_gpio_write(hal_gpio_t* h, hal_gpio_level_t level);

/**
 * @brief 读电平
 */
hal_gpio_level_t hal_gpio_read(hal_gpio_t* h);

/**
 * @brief 翻转
 */
void hal_gpio_toggle(hal_gpio_t* h);

/* 可选：断言失败钩子（弱符号由 port 层提供默认实现，上层可覆盖） */
void hal_gpio_assert_failed(const char* file, int line);

/* ---------------- 中断回调注册 ---------------- */

/**
 * @brief  GPIO 中断回调函数原型
 * @param  user_data  注册时传入的用户数据
 */
typedef void (*hal_gpio_irq_cb_t)(void* user_data);

/**
 * @brief  注册 GPIO 中断回调
 * @param  h          GPIO 句柄
 * @param  cb         回调函数
 * @param  user_data  用户数据
 * @return RET_OK 或错误码
 * @note   调用前需通过 hal_gpio_config 配置为中断模式 (RISING/FALLING/BOTH)
 *         会自动使能 NVIC
 */
ret_code_t hal_gpio_register_irq(hal_gpio_t* h, hal_gpio_irq_cb_t cb, void* user_data);

/**
 * @brief  注销 GPIO 中断回调
 * @param  h  GPIO 句柄
 * @return RET_OK 或错误码
 * @note   会自动禁用 NVIC (如果该中断线上无其他引脚使用)
 */
ret_code_t hal_gpio_unregister_irq(hal_gpio_t* h);

/**
 * @brief 内部错误弱钩子函数
 * @param rc_port port 错误码
 * @param rc_hal  hal 错误码
 * @param api     api名称
 * @param arg0    参数1
 * @param arg1    参数2
 */
void hal_gpio_on_port_error(ret_code_t rc_port, ret_code_t rc_hal, const char* api, uint32_t arg0,
                            uint32_t arg1);

#ifdef __cplusplus
}
#endif
#endif
