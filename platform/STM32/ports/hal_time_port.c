#include <stdbool.h>
#include <stdint.h>

#include "log.h"
#include "stm32f4xx_hal.h"/* 可以更改不同系列 */
#include "stm32f4xx.h"
#include "utils_def.h"
/* dwt初始化标志位 */
static bool dwt_inited = false;
/* dwt 有效标志位 */
static bool dwt_available = true;

/**
 * 初始化 DWT寄存器
 */
static void dwt_init_once(void) {
    //DWT初始化
    BIT_SET(CoreDebug->DEMCR, 24); //使能DWT外设
    DWT->CYCCNT = 0;
    BIT_SET(DWT->CTRL, 0);
}

/**
 * @note 可以回绕 上层应该做好检查
 * @return 返回当前以 ms 为单位的时间戳
 */
uint32_t hal_get_tick_ms(void) {
    return HAL_GetTick();
}

/**
 * @note 可以回绕 上层应该做好检查
 * @return 返回当前以 us 为单位的时间戳
 */
uint32_t hal_get_tick_us32(void) {
    /* 判断系统主频是否正常 */
    if (SystemCoreClock == 0U) {
        return hal_get_tick_ms() * 1000U;
    }
    /* 没有初始化 DWT 就锁住初始化 */
    if (dwt_inited == false) {
        uint32_t primask = __get_PRIMASK();
        __disable_irq();

        if (dwt_inited == false) {
            dwt_init_once();

            uint32_t c1 = DWT->CYCCNT;
            __NOP();
            __NOP();
            __NOP();
            __NOP();
            __NOP();
            __NOP();
            __NOP();
            __NOP();
            uint32_t c2 = DWT->CYCCNT;
            /* 判断DWT 是否运行成功 */
            if (c2 == c1) {
                dwt_available = false;
            }

            dwt_inited = true;
        }
        if (primask == 0U) {
            __enable_irq();
        }
    }
    /* 运行失败退化为 hal _get_tick_ms() *1000 */
    if (dwt_available == false) {
        LOG_E("DWT", "DWT启动失败");
        return hal_get_tick_ms() * 1000U;
    }

    /* DWT 启动成功才采用该值 */
    const uint32_t us = (uint32_t) (((uint64_t) DWT->CYCCNT * 1000000ULL) / (uint64_t) SystemCoreClock);
    return us;
}
