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

/**
 * @brief 毫秒级阻塞延时
 * @param ms 延时时长
 * @note 严禁在 ISR 调用。在裸机下为死等，在 RTOS 下应被映射为 task_delay。
 */
void hal_time_delay_ms(uint32_t ms);

/**
 * @brief 微妙级阻塞延时
 * @param us 延时时长
 * @note 严禁在 ISR 调用。在裸机下为死等，在 RTOS 下应被映射为 task_delay。
 */
void hal_time_delay_us(uint32_t us);

#endif  // HAL_TIME_H
