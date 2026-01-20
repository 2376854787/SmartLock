#ifndef SMARTLOCK_CRC16_H
#define SMARTLOCK_CRC16_H
#include <stdbool.h>
#include <stdint.h>

#include "ret_code.h"
/* 结构体声明 */
typedef struct {
    uint16_t init;         /* 初始值 */
    uint16_t poly;         /* 多项式 */
    uint16_t res_op_value; /* 结果异或值 */
    bool isMsbFirst;       /* 是否高位在前 */
} crc16_config_t;

/* 默认参数配置 */
typedef enum {
    MODBUS = 0,
    CCITT,
    USB,
    MAXIM,
} crc16_config_default;

/**
 * @brief 将数据通过指定的crc配置编码后返回
 * @param cfg crc16配置结构体
 * @param data 将要进行编码的数据
 * @param length 字节数
 * @param out 保存计算结果
 * @return 返回crc校验码
 * @note 需要自定义参数注意 低位在前 poly 填写反向值 高位在前 poly 填写正向值
 */
ret_code_t crc16_cal(const crc16_config_t* cfg, const uint8_t* data, uint16_t length,
                     uint16_t* out);
/**
 * @brief 将数据通过指定的crc配置编码后返回
 * @param name 默认配置名称
 * @param data 将要进行编码的数据
 * @param length 字节数
 * @param out 保存计算结果
 * @return 返回crc校验码
 * @note 预配置参数
 */
ret_code_t crc16_cal_default(crc16_config_default name, const uint8_t* data, uint16_t length,
                             uint16_t* out);
/**
 * @brief 将数据通过指定的crc配置编码后返回 ---查表法
 * @param cfg crc16配置结构体
 * @param data 将要进行编码的数据
 * @param length 字节数
 * @param out 保存计算结果
 * @return 返回crc校验码
 * @note 需要自定义参数注意 低位在前 poly 填写反向值 高位在前 poly 填写正向值
 */
ret_code_t crc16_cal_table(const crc16_config_t* cfg, const uint8_t* data, uint16_t length,
                           uint16_t* out);
/**
 * @brief 将数据通过指定的crc配置编码后返回 -- 查表法
 * @param name 默认配置名称
 * @param data 将要进行编码的数据
 * @param length 字节数
 * @param out 保存计算结果
 * @return 返回crc校验码
 * @note 预配置参数
 */
ret_code_t crc16_cal_default_table(crc16_config_default name, const uint8_t* data, uint16_t length,
                                   uint16_t* out);
#endif  // SMARTLOCK_CRC16_H
