#ifndef BH1750_BH1750_H
#define BH1750_BH1750_H
#include <stdint.h>

void BH1750_Init(void);
/**
 * @brief 发送重置命令
 */
void BH1750_Reset(void);
/**
 * @brief 发送 关机命令
 */
void BH1750_PowerDown(void);
/**
 * @brief 发送上电命令
 */
void BH1750_PowerOn(void);
/**
 * @brief 设置为高精度连续采集模式
 * @note 1lx精度 120ms采集时间
 */
void BH1750_Set_CONT_HIRES_MODE(void);
/**
 * @brief 设置为高精度连续采集模式2
 * @note 0.5lx精度 120ms采集时间
 */
void BH1750_Set_CONT_HIRES_MODE2(void);
/**
 * @brief 持续低精度采集模式
 * @note 4lx精度 16ms采集时间
 */
void BH1750_Set_CONT_LORES_MODE(void);
/**
 * @brief 一次高精度连续采集模式
 * @note 1lx精度 120ms采集时间
 *       采集后会进入 PowerDown
 */
void BH1750_Set_ONE_HIRES_MODE(void);
/**
 * @brief 一次高精度连续采集模式2
 * @note 0.5lx精度 120ms采集时间
 *       采集后会进入 PowerDown
 */
void BH1750_Set_ONE_HIRES_MODE2(void);
/**
 * @brief 一次低精度采集模式
 * @note 4lx精度 16ms采集时间
 *       采集后会进入 PowerDown
 */
void BH1750_Set_ONE_LORES_MODE(void);
/**
 * @brief  获取光照LX
 * @return 光照LX
 * @note 低精度至高采集时间24ms 高精度180ms
 */
float BH1750_Get_LX(void);
/**
 * @brief 设置采集时间的高3位
 * @param time 采集时间
 */
void BH1750_Set_MTREG_H(uint8_t time);
/**
 * @brief 设置采集时间的低5位
 * @param time 采集时间
 */
void BH1750_Set_MTREG_L(uint8_t time);
#endif
