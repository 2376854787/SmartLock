/**
 * @file    gt911.h
 * @brief   GT911 电容触控驱动 (平台无关)
 *
 * 依赖:
 *   - soft_i2c.h  (软件 I2C 总线)
 *   - hal_gpio.h  (RST / INT 引脚控制)
 *   - hal_time.h  (延时)
 *
 * 使用步骤:
 *   1. 定义 gt911_dev_t 实例
 *   2. 填充 gt911_cfg_t 并调用 gt911_init()
 *   3. 在触摸中断或轮询任务中调用 gt911_read_touch()
 */

#ifndef GT911_H
#define GT911_H

#include <stdbool.h>
#include <stdint.h>

#include "ret_code_t.h"
#include "soft_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ======================== 常量 ======================== */

#define GT911_MAX_TOUCH_POINTS 5u /**< GT911 最大支持触点数 */

/** GT911 常用 7-bit I2C 地址 */
#define GT911_ADDR_HIGH 0x14u /**< INT 复位期间为 HIGH 时 */
#define GT911_ADDR_LOW  0x5Du /**< INT 复位期间为 LOW 时 (默认) */

/* ======================== 数据类型 ======================== */

/** 单个触点信息 */
typedef struct {
    uint16_t x;    /**< X 坐标 */
    uint16_t y;    /**< Y 坐标 */
    uint16_t size; /**< 触摸面积 */
    uint8_t id;    /**< 触点 ID (0..4) */
} gt911_point_t;

/** 一次触摸读取的全部数据 */
typedef struct {
    uint8_t count;                                /**< 当前触点数 (0..5) */
    gt911_point_t points[GT911_MAX_TOUCH_POINTS]; /**< 触点数组 */
} gt911_touch_data_t;

/** GT911 初始化配置 */
typedef struct {
    uint32_t gpio_id_scl; /**< board GPIO ID: I2C SCL */
    uint32_t gpio_id_sda; /**< board GPIO ID: I2C SDA */
    uint32_t gpio_id_rst; /**< board GPIO ID: RST 复位引脚 */
    uint32_t gpio_id_int; /**< board GPIO ID: INT 中断引脚 */
    uint8_t i2c_addr;     /**< 7-bit I2C 地址 (GT911_ADDR_LOW 或 GT911_ADDR_HIGH) */
    uint16_t max_x;       /**< 屏幕 X 方向分辨率 */
    uint16_t max_y;       /**< 屏幕 Y 方向分辨率 */
    uint8_t refresh_rate; /**< 刷新率 (周期 ms), 典型的 10~20ms */
} gt911_cfg_t;

/** GT911 设备实例（调用者分配存储） */
typedef struct gt911_dev {
    soft_i2c_t i2c;   /**< 内嵌 soft_i2c 实例 */
    hal_gpio_t* rst;  /**< RST 引脚句柄 */
    hal_gpio_t* intr; /**< INT 引脚句柄 */
    uint8_t addr;     /**< 7-bit I2C 地址 */
    uint16_t max_x;   /**< 屏幕分辨率 X */
    uint16_t max_y;   /**< 屏幕分辨率 Y */
} gt911_dev_t;

/* ======================== API ======================== */

/**
 * @brief  初始化 GT911 触控芯片
 * @param  dev  调用者分配的设备实例
 * @param  cfg  配置参数
 * @return RET_OK 或错误码
 * @note   内部执行:
 *         1. 通过 RST/INT 时序选择 I2C 地址
 *         2. 初始化 Soft I2C 总线
 *         3. 读取 Product ID 验证通信
 */
ret_code_t gt911_init(gt911_dev_t* dev, const gt911_cfg_t* cfg);

/**
 * @brief  读取当前触摸数据
 * @param  dev  设备实例
 * @param  out  输出触摸数据
 * @return RET_OK（即使无触摸也返回 OK，此时 out->count == 0）
 */
ret_code_t gt911_read_touch(gt911_dev_t* dev, gt911_touch_data_t* out);

/**
 * @brief  读取 GT911 Product ID
 * @param  dev      设备实例
 * @param  id_buf   接收缓冲区 (至少 4 字节)
 * @param  buf_len  缓冲区长度
 * @return RET_OK 或 I2C 通信错误码
 * @note   正常应读到 "9111" 或 "911\0"
 */
ret_code_t gt911_read_product_id(gt911_dev_t* dev, char* id_buf, uint32_t buf_len);

#ifdef __cplusplus
}
#endif

#endif /* GT911_H */
