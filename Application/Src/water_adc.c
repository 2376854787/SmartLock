//
// 创建：yan，2025/12/3
//

#include "water_adc.h"

#include <stdio.h>

#include "adc.h"
#include "cmsis_os2.h"
#include "lcd.h"
#include "stm32f4xx_hal_adc.h"

#define VREFINT_TYPICAL_MV 1210U
#define ADC_TIMEOUT_MS     10U
#define ADC_FULL_SCALE     4095.0f

static volatile uint32_t raw;
/**
 *@brief  单次采样指定通道，轮询方式
 */
static uint32_t read_adc_single(uint32_t channel, uint32_t sample_time) {
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = channel;
    sConfig.Rank = 1;
    sConfig.SamplingTime = sample_time;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
        return 0;
    }

    if (HAL_ADC_Start(&hadc1) != HAL_OK) return 0;
    if (HAL_ADC_PollForConversion(&hadc1, ADC_TIMEOUT_MS) != HAL_OK) return 0;
    return HAL_ADC_GetValue(&hadc1);
}

/**
 * @brief 通过VREFINT换算当前VDDA（mV）
 */
static uint32_t measure_vdda_mv(void) {
    uint32_t vref_raw = read_adc_single(ADC_CHANNEL_VREFINT, ADC_SAMPLETIME_480CYCLES);
    if (vref_raw == 0) return 3300U; // 兜底返回3.3V
    return (uint32_t) ((VREFINT_TYPICAL_MV * ADC_FULL_SCALE) / vref_raw);
}

/**
 * @brief RTOS 水滴传感器采集任务
 */
void waterSensor_task(void *argument) {
    for (;;) {
        uint32_t vdda_mv = measure_vdda_mv();

        raw = read_adc_single(ADC_CHANNEL_1, ADC_SAMPLETIME_56CYCLES);
        uint32_t sensor_mv = (uint32_t) ((raw * vdda_mv) / ADC_FULL_SCALE);

        lcd_show_num(10, 200, raw, 4, 32, GREEN); // 原始值
        lcd_show_num(10, 240, sensor_mv, 4, 32, GREEN); // 换算电压(mV)
        lcd_show_num(10, 280, vdda_mv, 4, 32, GREEN); // 参考电压
        osDelay(200);
    }
}


/**
 * @brief HAL库回调函数占位（当前任务使用轮询，不再靠中断）
 * @param hadc ADC句柄
 */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc) {
    (void) hadc;
}
