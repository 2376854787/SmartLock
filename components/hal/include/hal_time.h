//
// Created by yan on 2025/12/31.
//

#ifndef HAL_TIME_H
#define HAL_TIME_H
#include <stdint.h>

/**
 * @note 可以回绕 上层应该做好检查
 * @return 返回当前以 ms 为单位的时间戳
 */
uint32_t hal_get_tick_ms(void);
/**
 * @note 可以回绕 上层应该做好检查
 * @return 返回当前以 us 为单位的时间戳
 */
uint32_t hal_get_tick_us32(void);

/* 判断 a 是否在 b 的前面或者后面 */
#define HAL_TIME_AFTER_EQ(a, b)  ((int32_t)((uint32_t)(a) - (uint32_t)(b)) >= 0)
#define HAL_TIME_BEFORE(a, b)    ((int32_t)((uint32_t)(a) - (uint32_t)(b)) < 0)

#endif //SMARTLOCK_HAL_TIME_H
