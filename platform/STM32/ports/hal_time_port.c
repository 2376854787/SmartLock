#include "APP_config.h"
#include "stm32_hal_config.h"
/* hal抽象选择宏 */
#if defined(USE_STM32_HAL) && defined(ENABLE_HAL_TIME)
#include <stdbool.h>
#include <stdint.h>

#include "log.h"
#include "osal.h"
#include "stm32f4xx.h"
#include "stm32f4xx_hal.h" /* 可以更改不同系列 */
#include "utils_def.h"
/* dwt初始化标志位 */
static bool dwt_inited      = false;
/* dwt 有效标志位 */
static bool dwt_available   = false;
/* 只打印一次失败信息 */
static bool dwt_fail_logged = false;
/**
 * 初始化 DWT寄存器
 */
static void dwt_init_once(void) {
    // DWT初始化
    BIT_SET(CoreDebug->DEMCR, 24);  // 使能DWT外设
    DWT->CYCCNT = 0;
    BIT_SET(DWT->CTRL, 0);
    __DSB();
    __ISB();
}

/**
 * @note 可以回绕 上层应该做好检查(无符号 处理)
 * @return 返回当前以 ms 为单位的时间
 */
uint32_t hal_get_tick_ms(void) {
    return HAL_GetTick();
}

/**
 * @note 可以回绕 上层应该做好检查（无符号处理）
 * @return 返回当前以 us 为单位的时间
 */
uint32_t hal_get_tick_us32(void) {
    /* 判断系统主频是否正常 */
    if (SystemCoreClock == 0U) {
        return hal_get_tick_ms() * 1000U;
    }

    /* ISR 不做初始化，避免拉长中断 */
    if (!dwt_inited) {
        if (OSAL_in_isr()) {
            return hal_get_tick_ms() * 1000U;
        }

        /* 用可恢复临界区保护一次性初始化 */
        osal_crit_state_t s;
        OSAL_enter_critical_ex(&s);
        /* 没有初始化 DWT 初始化 */
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
            if (c2 != c1) {
                dwt_available = true;
            } else {
                dwt_available = false;
            }

            dwt_inited = true;
            __DMB(); /* 写入 flags 后的可见性/顺序 */
        }
        OSAL_exit_critical_ex(s);
    }
    /* 运行失败退化为 hal _get_tick_ms() *1000 */
    if (dwt_available == false) {
        if (!dwt_fail_logged) {
            dwt_fail_logged = true;
            LOG_E("DWT", "DWT启动失败，降级到 HAL_GetTick()*1000");
        }
        return hal_get_tick_ms() * 1000U;
    }
    /* DWT 启动成功才采用该值 */
    const uint32_t cycles_per_us = SystemCoreClock / 1000000U;
    if (cycles_per_us == 0U) return hal_get_tick_ms() * 1000U;
    const uint32_t us = (uint32_t)DWT->CYCCNT / cycles_per_us;
    return us;
}

#else
#include <stdint.h>

uint32_t hal_get_tick_ms(void) {
    return 0U;
}

uint32_t hal_get_tick_us32(void) {
    return 0U;
}

void hal_time_delay_ms(uint32_t ms) {
    (void)ms;
}

void hal_time_delay_us(uint32_t us) {
    (void)us;
}

#endif
