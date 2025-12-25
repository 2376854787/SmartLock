//
// Created by yan on 2025/11/22.
//

#include "My_delay.h"

#include <stdbool.h>

#include "cmsis_os2.h"
#include "FreeRTOSConfig.h"
#include "main.h"
#include "FreeRTOS.h"


#define SYS_SUPPORT_OS 1
uint32_t delay_osintnesting = 0;
bool delay_osrunning = false;

uint32_t g_fac_us, g_fac_ms;

/**
 * @brief 暂停RTOS任务调度
 */
static void delay_osschedlock(void) {
    osKernelLock();
}

/**
 * @brief 启用RTOS任务调度
 */
static void delay_osschedunlock(void) {
    osKernelUnlock();
}

/**
 * @brief 使用RTOS系统非阻塞延时
 * @param ticks 系统Ticks
 */
static void delay_ostimedly(uint32_t ticks) {
    osDelay(ticks);
}

/**
 * @brief 进行自定义延时函数的初始化配置
 * @param sysclk 通常为系统时钟频率（MHZ）
 */
void delay_init(uint16_t sysclk) {
#if SYS_SUPPORT_OS
    uint32_t reload;
#endif
    g_fac_us = sysclk;
#if SYS_SUPPORT_OS
    reload = sysclk;
    reload *= 1000000 / (uint32_t) configTICK_RATE_HZ; /* 1s为1_000_000 us  除以RTOS的心跳频率结果为每SysTick的us数
                                                        reload表示每SysTick发生一次系统滴答寄存器的变化次数*/
    g_fac_ms = 1000 / (uint32_t) configTICK_RATE_HZ; /* 这里计算每SysTick的ms时间 */

    // SysTick->CTRL |= 1 << 1; /* 开启 SysTick 中断 */
    // SysTick->LOAD = reload; /* 设置滴答定时器一次中断时间为一次系统心跳的时间*/
    // /* 每 1/configTICK_RATE_HZ 秒中断一次 */
    // SysTick->CTRL |= 1 << 0; /* 开启 SysTick */
#endif
}

/**
 * @brief 进行微妙延时
 * @param nus 延时微妙数
 */
void delay_us(uint32_t nus) {
    uint32_t told, tnow, tcnt = 0;
    const uint32_t reload = SysTick->LOAD; /*168mhz主频下为 168000 */
    const uint32_t Ticks = nus * g_fac_us;
#if SYS_SUPPORT_OS
    delay_osschedlock();
#endif
    told = SysTick->VAL; /* 刚开始的计数值 */
    while (1) {
        tnow = SysTick->VAL;
        if (tnow != told) {
            /* 判断当前是否走完了设定时间应该完成的Systick寄存器的变化值 */
            if (tnow < told) {
                /* 没有回环 */
                tcnt += told - tnow;
            } else {
                /* 回环了 */
                tcnt += reload - tnow + told;
            }
            told = tnow;
            if (tcnt >= Ticks) {
                /* 对应tick到了 */
                break;
            }
        }
    }
#if SYS_SUPPORT_OS
    delay_osschedunlock();
#endif
}

/**
 * @brief 进行ms级别延时 延时时间大于1 SysTick使用系统非阻塞延时 小于则使用阻塞式轮询空转
 * @param nms 延时ms数
 */
void delay_ms(uint16_t nms) {
#if SYS_SUPPORT_OS /* 如果需要支持 OS, 则根据情况调用 os 延时以释放 CPU */
    if (delay_osrunning && (__get_IPSR() == 0))
    /* 如果 OS 已经在跑了,并且不是在中断里面(中断里面不能任务调度) */
    {
        if (nms >= g_fac_ms) /* 延时的时间大于 OS 的最少时间周期 */
        {
            delay_ostimedly(nms / g_fac_ms); /* OS 延时 */
        }
        nms %= g_fac_ms;
        /* OS 已经无法提供这么小的延时了,采用普通方式延时 */
    }
#endif
    delay_us((uint32_t) (nms * 1000)); /* 普通方式延时 */
}
