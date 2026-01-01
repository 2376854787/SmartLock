/**
****************************************************************************************************
 * @file        touch_port.h
 * @brief       Touch(CTP) RTOS port layer: mutex + delay adapters
 *
 * 用法:
 *   1) 如果项目使用 FreeRTOS 请定义: TOUCH_OS_FREERTOS
 *   2) 如果项目使用 CMSIS-RTOS2 请定义: TOUCH_OS_CMSIS_OS2
 *   3) 如果项目使用 uC/OS-II 请定义: TOUCH_OS_UCOSII
 *   4) 不定义任何宏时，默认裸机模式（不使用互斥锁，delay 调用原 delay_ms/us）
 *
 * 为了在 RTOS 中保证软件 I2C(bit-bang)稳定，对一次 I2C 事务(start->stop)
 * 必须加锁。本文件只提供通用的 lock/unlock/delay 封装。
 ****************************************************************************************************
 */

#ifndef __TOUCH_PORT_H
#define __TOUCH_PORT_H

#include <stdint.h>

/* 裸机 delay 依赖 */
#include "My_delay.h"

#if defined(TOUCH_OS_FREERTOS)
  #include "FreeRTOS.h"
  #include "task.h"
  #include "semphr.h"
#elif defined(TOUCH_OS_CMSIS_OS2)
  #include "cmsis_os2.h"
#elif defined(TOUCH_OS_UCOSII)
  /* 兼容 uC/OS-II: 请在你的工程中提供 os.h */
  #include "os.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

    void touch_port_init(void);
    void touch_port_lock(void);
    void touch_port_unlock(void);

    /* ms 级延时：RTOS 下尽量让出 CPU，裸机下用 delay_ms */
    void touch_port_delay_ms(uint32_t ms);

    /* us 级延时：保留忙等（软 I2C 依赖边沿时序） */
    static inline void touch_port_delay_us(uint32_t us)
    {
        delay_us(us);
    }

    /* 获取毫秒 tick（用于日志/超时等） */
    uint32_t touch_port_get_tick_ms(void);

#ifdef __cplusplus
}
#endif

#endif
