#ifndef HAL_TIME_H
#define HAL_TIME_H

#include <stdint.h>
/* 判断 a 是否在 b 的前面或者后面 */
#define HAL_TIME_AFTER_EQ(a, b) ((int32_t)((uint32_t)(a) - (uint32_t)(b)) >= 0)
#define HAL_TIME_BEFORE(a, b)   ((int32_t)((uint32_t)(a) - (uint32_t)(b)) < 0)

/**
 * @brief 获取系统启动以来的毫秒数
 * @return 32位毫秒计数值（约49天回绕一次）
 * @note 线程安全，在 ISR 和任务中均可调用 需要处理回绕
 */
uint32_t hal_get_tick_ms(void);
/**
 * @note 可以回绕 上层应该做好检查
 * @return 返回当前以 us 为单位的时间戳
 */
uint32_t hal_get_tick_us32(void);
uint64_t hal_get_tick_us64(void);

/**
 * @brief 毫秒级阻塞延时
 * @param ms 延时时长
 * @note 严禁在 ISR 调用。在裸机下为死等，在 RTOS 下应被映射为 task_delay。
 */
void hal_time_delay_ms(uint32_t ms);

/**
 * @brief 微妙级阻塞延时
 * @param us 延时时长
 * @note 严禁在 ISR 调用。< 1ms轮询死等 否则调用 hal_time_delay_ms()
 */
void hal_time_delay_us(uint32_t us);

/**
 * @brief 获取 内核周期
 * @return 返回 DWT->CYCCNT 寄存器实时的存储值
 * @note 使用前必须确保 DWT 初始化成功
 */
uint32_t hal_get_cycle32(void);

/**
 * @brief 提供任意合法主频下的us周期
 * @return 当前主频下每us 的周期数
 * @note 使用前必须确保 DWT 初始化成功
 */
uint32_t hal_get_cycles_per_us(void);

/**
 * @brief 任意合法主频下将周期转换为 us
 * @param cyc 周期数
 * @return us 数
 * @note 使用前必须确保 DWT 初始化成功
 */
uint32_t hal_cycles_to_us(uint32_t cyc);
/**
 * @brief 初始化 time 抽象（一般为DWT高精度时钟） 底层保证只会初始化一次
 * @note 要求在 SystemClock_Config() 之后、首次调用 hal_get_tick_us32() 之前执行一次
 */
void hal_time_init(void);

#endif  // HAL_TIME_H
