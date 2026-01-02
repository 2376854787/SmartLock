//
// 创建：yan，2025/12/20
//

#ifndef SMARTLOCK_CONFIG_CUS_H
#define SMARTLOCK_CONFIG_CUS_H
/* 功能开启宏  取消注释即开启整个项目的 指定功能*/
#define ENABLE_LOG_SYSTEM               /* 日志管理系统 */
#define ENABLE_AT_SYSTEM                /* AT框架管理系统 */
#define ENABLE_ASSERT_SYSTEM            /* 断言管理系统 */
#define ENABLE_STATIC_ALLOCATION        /* 静态内存分配 */
#define ENABLE_RINGBUFFER_SYSTEM        /* 环形缓冲区系统 */
#define ENABLE_HFSM_SYSTEM              /* HFSM系统 */
#define ENABLE_KEYS                     /* 使能按键系统 */
#define ENABLE_ASSERT_SYSTEM            /* 使能断言系统 */


/* 协议标准配置宏  取消注释即开启整个项目的 执行标准*/
/* 启动CMSIS v2 标准 */
#define OSAL_BACKEND_CMSIS_OS2        1      /* osal_cmsis2.c */
#define TOUCH_OS_CMSIS_OS2
/* 裸机 和 RTOS环境配置宏 */
/* 1 使用 FreeRTOS 的taskENTER_CRITICAL()/FROM_ISR
 * 0 使用 PRIMASK  全关中断 （延时更大）
 */
#define OSAL_CRITICAL_IMPL_FREERTOS   1  /* osal_cmsis2.c */


#define USED_STM32_PLATFORM       /* 启用STM32平台 */

/* ================= 触摸/LVGL 输入调谐=================
 * GT9xxx（GT9147...）触控坐标映射可能因模块布线而异。
 * 本项目中，LCD默认为横屏（800x480），触控键
 * 坐标通常在交换后镜像在 X 中。
 */
#ifndef TOUCH_POLL_INTERVAL_MS
#define TOUCH_POLL_INTERVAL_MS 5u
#endif

/* 在 X / Y 切换后应用，当 LCD 为横向时 （lcddev.dir==1） */
#ifndef TOUCH_LANDSCAPE_INVERT_X
#define TOUCH_LANDSCAPE_INVERT_X 1
#endif

#ifndef TOUCH_LANDSCAPE_INVERT_Y
#define TOUCH_LANDSCAPE_INVERT_Y 0
#endif

#ifndef TOUCH_PORTRAIT_INVERT_X
#define TOUCH_PORTRAIT_INVERT_X 0
#endif

#ifndef TOUCH_PORTRAIT_INVERT_Y
#define TOUCH_PORTRAIT_INVERT_Y 0
#endif
#endif //SMARTLOCK_CONFIG_CUS_H
