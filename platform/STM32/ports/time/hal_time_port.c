#include "APP_config.h"
#include "stm32_hal_config.h"
/* hal抽象选择宏 */
#if (defined(CFG_TARGET_PLATFORM_STM32_HAL) && (CFG_TARGET_PLATFORM_STM32_HAL == 1)) && \
    (defined(CFG_FEAT_HAL_TIME) && (CFG_FEAT_HAL_TIME == 1))
#include <stdbool.h>
#include <stdint.h>
#include "assert_cus.h"
#include "barrier.h"
#include "cmsis_os2.h"
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
/* 运行时间重新校准与单调时间状态 */
static uint32_t dwt_last_sysclk = 0u; /* 上次记录的系统主频 */
static uint64_t dwt_us_accum    = 0u; /* 累计的us总数 */
static uint32_t dwt_last_cyccnt = 0u; /* 上次记录的周期数 */
static uint32_t dwt_cycle_rem   = 0u; /* 周期换算余数减少截断误差 */
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
    if (cycles_per_us == 0u) cycles_per_us = 1u;
    dwt_last_sysclk = SystemCoreClock;
    dwt_last_cyccnt = DWT->CYCCNT;
    dwt_us_accum    = 0u;
    dwt_cycle_rem   = 0u;
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
 * @brief 判断系统主频是否变化 然后更新全局参数
 */
static void dwt_recalibrate_if_needed(void) {
    /* 判断系统主频是否变化 */
    const uint32_t cur_sysclk = SystemCoreClock;
    if (CORE_UNLIKELY(cur_sysclk == 0u)) return;
    if (CORE_LIKELY(cur_sysclk == dwt_last_sysclk)) return;
    /*　进入临界区　*/
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    /* 内存屏障 */
    mem_barrier();
    /* 重新计算每us的周期数 */
    if (CORE_UNLIKELY(cur_sysclk != dwt_last_sysclk)) {
        uint32_t cpu_per_us = cur_sysclk / 1000000u;
        if (cpu_per_us == 0u) cpu_per_us = 1u;
        cycles_per_us   = cpu_per_us;
        dwt_last_sysclk = cur_sysclk;
        if (dwt_cycle_rem >= cpu_per_us) dwt_cycle_rem %= cpu_per_us;
        mem_barrier();
    }
    __set_PRIMASK(primask);
}

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
    dwt_recalibrate_if_needed();
    if (CORE_UNLIKELY(cycles_per_us == 0U)) return hal_get_tick_ms() * 1000U;
    /* 获取当前的周期数 */
    const uint32_t now_cyc = DWT->CYCCNT;
    /* 进入临界区 */
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    mem_barrier();
    /* 计算相距的 DWT 周期 */
    const uint32_t delta_cyc = now_cyc - dwt_last_cyccnt;
    dwt_last_cyccnt          = now_cyc;
    /* 计算总周期数 = 余数 + 这次相差的周期 */
    const uint64_t sum_cyc   = (uint64_t)dwt_cycle_rem + (uint64_t)delta_cyc;
    /* 计算出这次可以除尽的 us 数 */
    const uint32_t inc_us    = (uint32_t)(sum_cyc / cycles_per_us);
    /* 更新余数 */
    dwt_cycle_rem            = (uint32_t)(sum_cyc - ((uint64_t)inc_us * (uint64_t)cycles_per_us));
    /* 更新总us数 */
    dwt_us_accum += (uint64_t)inc_us;
    // const uint32_t us32 = (uint32_t)dwt_us_accum;

    mem_barrier();
    __set_PRIMASK(primask);
    return dwt_us_accum;
}
/**
 * @brief us级延时
 * @note CMSISv2 实现下为将 ms 转换为ticks后调用osDelay 的非阻塞延时
 *       裸机为 HAL_Delay 阻塞延时
 */
uint64_t hal_get_tick_us64(void) {
    (void)hal_get_tick_us32();
    return dwt_us_accum;
}
/**
 * @brief 启用 CFG_FEAT_OSAL_BACKEND_CMSIS_OS2=1 启用非阻塞延时 否则为阻塞延时
 * @param ms 延时的ms数
 */
void hal_time_delay_ms(uint32_t ms) {
#if (defined(CFG_FEAT_OSAL_BACKEND_CMSIS_OS2) && (CFG_FEAT_OSAL_BACKEND_CMSIS_OS2 == 1))
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
        // 粗略降级：1us 约等于 SystemCoreClock/3000000 次空循环 (针对 F4)
        volatile uint32_t count = us * (SystemCoreClock / 3000000U);
        while (count--) {
            __NOP();
        }
        return;
    }

    const uint32_t start_clk = DWT->CYCCNT; /* 直接拿最原始的 CPU Tick */
    /* 将 us 转换为 CPU Tick，避开在循环里反复做除法 */
    /* 检查主频是否发生了变化 */
    dwt_recalibrate_if_needed();
    /* 计算出需要等待的 实际周期数 */
    const uint64_t wait_ticks64 = (uint64_t)us * (uint64_t)cycles_per_us;
    /* 处理超出最大周期数的情况 */
    if (CORE_UNLIKELY(wait_ticks64 > 0xFFFFFFFFu)) {
        const uint32_t start_us = hal_get_tick_us32();
        /* 退化为直接使用 us 计算*/
        while ((uint32_t)(hal_get_tick_us32() - start_us) < us) {
            // wait
        }
        return;
    }
    const uint32_t wait_ticks = (uint32_t)wait_ticks64;

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
    const uint32_t cpu_per_us = hal_get_cycles_per_us();
    ASSERT_PARAM(cpu_per_us != 0u);
    REQUIRE_RET(cpu_per_us != 0u, cyc);
    return (uint32_t)(cyc + (uint64_t)cpu_per_us - 1u) / cpu_per_us;
}
#else
#include <stdint.h>

uint32_t hal_get_tick_ms(void) {
    return 0U;
}

uint32_t hal_get_tick_us32(void) {
    return 0U;
}

uint64_t hal_get_tick_us64(void) {
    return (uint64_t)hal_get_tick_us32();
}

void hal_time_delay_ms(uint32_t ms) {
    (void)ms;
}

void hal_time_delay_us(uint32_t us) {
    (void)us;
}

#endif
