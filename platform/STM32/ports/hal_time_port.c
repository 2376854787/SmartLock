#include "APP_config.h"
#include "stm32_hal_config.h"
/* hal抽象选择宏 */
#if defined(USE_STM32_HAL) && defined(ENABLE_HAL_TIME)
#include <stdbool.h>
#include <stdint.h>

#include "barrier.h"
#include "cmsis_os2.h"
#include "log.h"
#include "osal.h"
#include "stm32f4xx.h"
#include "stm32f4xx_hal.h"
#include "utils_def.h"
/* dwt初始化标志位 */
static bool dwt_inited      = false;
/* dwt 有效标志位 */
static bool dwt_available   = false;
/* 只打印一次失败信息 */
static bool dwt_fail_logged = false;
/* 当前主频下每 us的计数器数 */
static uint32_t cycles_per_us;
/**
 * 初始化 DWT寄存器
 */
void dwt_init_once(void) {
    // DWT初始化
    BIT_SET(CoreDebug->DEMCR, 24);  // 使能DWT外设
    DWT->CYCCNT = 0;
    BIT_SET(DWT->CTRL, 0);
    mem_barrier();
    inst_barrier();
    cycles_per_us = SystemCoreClock / 1000000U;
}

/**
 * @note 可以回绕 上层应该做好检查(无符号 处理)
 * @return 返回当前以 ms 为单位的时间
 */
uint32_t hal_get_tick_ms(void) {
    return HAL_GetTick();
}

/* dwt 初始化期间的递归检测守卫 */
static volatile bool dwt_init_in_progress = false;

/**
 * @note 可以回绕 上层应该做好检查（无符号处理）
 * @return 返回当前以 us 为单位的时间
 */
uint32_t hal_get_tick_us32(void) {
    /* 递归检测：如果正在初始化中被再次调用，直接返回降级值 */
    if (CORE_UNLIKELY(dwt_init_in_progress)) {
        return hal_get_tick_ms() * 1000U;
    }

    /* 判断系统主频是否正常 */
    if (CORE_UNLIKELY(SystemCoreClock == 0U)) {
        return hal_get_tick_ms() * 1000U;
    }

    /* ISR 不做初始化，避免拉长中断 */
    if (CORE_UNLIKELY(!dwt_inited)) {
        if (OSAL_in_isr()) {
            return hal_get_tick_ms() * 1000U;
        }

        /* 用裸 PRIMASK 保护一次性初始化
         * 注意：不能用 OSAL_enter_critical_ex()，因为它内部调用
         * OSAL_CritMon_Enter() -> hal_get_tick_us32()，会导致无限递归
         */
        const uint32_t primask = __get_PRIMASK();
        __disable_irq();
        mem_barrier();
        /* 没有初始化 DWT 初始化 */
        if (dwt_inited == false) {
            dwt_init_in_progress = true; /* 设置守卫：防止递归 */
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

            dwt_inited           = true;
            dwt_init_in_progress = false; /* 清除守卫 */
            mem_barrier();                /* 写入 flags 后的可见性/顺序 */
        }
        mem_barrier();
        __set_PRIMASK(primask);
    }
    /* 运行失败退化为 hal _get_tick_ms() *1000 */
    if (CORE_UNLIKELY(dwt_available == false)) {
        if (!dwt_fail_logged) {
            dwt_fail_logged = true;
            // 禁止底层调用叶子节点 LOG_E("DWT", "DWT启动失败，降级到 HAL_GetTick()*1000");
        }
        return hal_get_tick_ms() * 1000U;
    }
    /* DWT 启动成功才采用该值 */
    if (CORE_UNLIKELY(cycles_per_us == 0U)) return hal_get_tick_ms() * 1000U;
    const uint32_t us = (uint32_t)DWT->CYCCNT / cycles_per_us;
    return us;
}
/**
 * @brief ms级延时
 * @param ms 需要延时的时间
 * @note CMSISv2 实现下为将 ms 转换为ticks后调用osDelay 的非阻塞延时
 *       裸机为 HAL_Delay 阻塞延时
 */
void hal_time_delay_ms(uint32_t ms) {
#if defined(OSAL_BACKEND_CMSIS_OS2)
    OSAL_delay_ms(ms);
#else
    return HAL_Delay(ms);
#endif
}

/**
 * @brief us级别延时
 * @param us
 */
void hal_time_delay_us(uint32_t us) {
    if (CORE_UNLIKELY(!dwt_available)) {
        hal_get_tick_us32();
    }

    /* 降级逻辑：如果 DWT 确实不可用 */
    if (CORE_UNLIKELY(!dwt_available)) {
        LOG_E("time", "DWT初始化失败");
        // 粗略降级：1us 约等于 SystemCoreClock/3000000 次空循环 (针对 F4)
        volatile uint32_t count = us * (SystemCoreClock / 3000000U);
        while (count--) {
            __NOP();
        }
        return;
    }

    const uint32_t start_clk  = DWT->CYCCNT; /* 直接拿最原始的 CPU Tick */
    /* 将 us 转换为 CPU Tick，避开在循环里反复做除法 */
    const uint32_t wait_ticks = us * cycles_per_us;

    /* 利用无符号减法处理回绕 */
    while (DWT->CYCCNT - start_clk < wait_ticks) {
        // 等待
    }
}
/**
 * @brief 获取 内核周期
 * @return 返回 DWT->CYCCNT 寄存器实时的存储值
 * @note 使用前必须确保 DWT 初始化成功
 */
uint32_t hal_get_cycle32(void) {
    return DWT->CYCCNT;
}

/**
 * @brief 提供任意合法主频下的us周期
 * @return 当前主频下每us 的周期数
 * @note 使用前必须确保 DWT 初始化成功
 */
uint32_t hal_get_cycles_per_us(void) {
    return cycles_per_us;
}

/**
 * @brief 任意合法主频下将周期转换为 us
 * @param cyc 周期数
 * @return us 数
 * @note 使用前必须确保 DWT 初始化成功
 */
uint32_t hal_cycles_to_us(uint32_t cyc) {
    return cyc + (uint64_t)hal_get_cycles_per_us() - 1 / hal_get_cycles_per_us();
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
