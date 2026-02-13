/**
 * @file    soft_i2c.h
 * @brief   平台无关的软件 I2C (bit-banging) 接口
 *
 * 依赖:
 *   - hal_gpio.h  (开漏 GPIO 操作)
 *   - hal_time.h  (微秒级延时)
 *
 * 使用步骤:
 *   1. 定义 soft_i2c_t 实例（由调用者分配存储）
 *   2. 填充 soft_i2c_cfg_t 并调用 soft_i2c_init()
 *   3. 使用 soft_i2c_write_reg / soft_i2c_read_reg 访问设备寄存器
 */

#ifndef SOFT_I2C_H
#define SOFT_I2C_H

#include <stdint.h>

#include "hal_gpio.h"
#include "osal.h"
#include "ret_code.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ======================== 配置 ======================== */

/**
 * @brief 软件 I2C 配置
 */
typedef struct {
    uint32_t gpio_id_scl; /**< board GPIO ID: SCL 引脚 */
    uint32_t gpio_id_sda; /**< board GPIO ID: SDA 引脚 */
    uint32_t delay_us;    /**< 半周期延时 (μs)  100kHz→5  400kHz→1 */
} soft_i2c_cfg_t;

/* ======================== 句柄 ======================== */

/**
 * @brief 软件 I2C 总线实例（调用者分配，内部初始化）
 */
typedef struct soft_i2c {
    hal_gpio_t* scl;    /**< SCL GPIO 句柄 */
    hal_gpio_t* sda;    /**< SDA GPIO 句柄 */
    uint32_t delay_us;  /**< 半周期延时 */
    osal_mutex_t mutex; /**< RTOS 互斥锁，保护总线并发访问 */
} soft_i2c_t;

/* ======================== API ======================== */

/**
 * @brief  初始化软件 I2C 总线
 * @param  bus  调用者分配的实例
 * @param  cfg  配置参数
 * @return RET_OK 或错误码
 * @note   SCL/SDA 将被配置为开漏+上拉输出，空闲释放为高
 */
ret_code_t soft_i2c_init(soft_i2c_t* bus, const soft_i2c_cfg_t* cfg);

/**
 * @brief  向设备写入数据（无寄存器地址）
 * @param  bus       总线实例
 * @param  dev_addr  7-bit 设备地址
 * @param  data      写入数据缓冲区
 * @param  len       数据长度
 * @return RET_OK / NACK 错误
 */
ret_code_t soft_i2c_write(soft_i2c_t* bus, uint8_t dev_addr, const uint8_t* data, uint32_t len);

/**
 * @brief  从设备读取数据（无寄存器地址）
 * @param  bus       总线实例
 * @param  dev_addr  7-bit 设备地址
 * @param  data      读取缓冲区
 * @param  len       期望读取长度
 * @return RET_OK / NACK 错误
 */
ret_code_t soft_i2c_read(soft_i2c_t* bus, uint8_t dev_addr, uint8_t* data, uint32_t len);

/**
 * @brief  写寄存器（16-bit 大端寄存器地址，GT911 等设备使用）
 * @param  bus       总线实例
 * @param  dev_addr  7-bit 设备地址
 * @param  reg       16-bit 寄存器地址
 * @param  data      写入数据
 * @param  len       数据长度
 * @return RET_OK / NACK 错误
 */
ret_code_t soft_i2c_write_reg16(soft_i2c_t* bus, uint8_t dev_addr, uint16_t reg,
                                const uint8_t* data, uint32_t len);

/**
 * @brief  读寄存器（16-bit 大端寄存器地址，GT911 等设备使用）
 * @param  bus       总线实例
 * @param  dev_addr  7-bit 设备地址
 * @param  reg       16-bit 寄存器地址
 * @param  data      读取缓冲区
 * @param  len       期望读取长度
 * @return RET_OK / NACK 错误
 */
ret_code_t soft_i2c_read_reg16(soft_i2c_t* bus, uint8_t dev_addr, uint16_t reg, uint8_t* data,
                               uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* SOFT_I2C_H */
