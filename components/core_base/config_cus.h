#ifndef SMARTLOCK_CONFIG_CUS_H
#define SMARTLOCK_CONFIG_CUS_H
/* 功能开启宏  取消注释即开启整个项目的 指定功能*/
#define ENABLE_LOG_SYSTEM        /* 日志管理 */
#define ENABLE_AT_SYSTEM         /* AT框架管理 */
#define ENABLE_ASSERT_SYSTEM     /* 断言管理 */
#define ENABLE_STATIC_ALLOCATION /* 静态内存分配 */
#define ENABLE_RINGBUFFER_SYSTEM /* 环形缓冲区 */
#define ENABLE_HFSM_SYSTEM       /* HFSM系统 */
#define ENABLE_KEYS              /* 使能按键 */
#define ENABLE_CRC16             /* 启动CRC16 */
#define ENABLE_MEMORY_POOL       /* 启动内存池 */

/* 自定义实现的 HAL 层功能启用 */
#define USE_STM32_HAL
#define ENABLE_HAL_WDG        /* 启动看门狗 */
#define ENABLE_WDG_SUPERVISOR /* 启动支持 */
#define ENABLE_HAL_GPIO       /* 启动GPIO */
#define ENABLE_HAL_UART       /* 启动串口 */
#define ENABLE_HAL_TIME       /* 启动time */
#define ENABLE_SOFT_I2C       /* 软件 I2C && ENABLE_HAL_GPIO*/
#define ENABLE_GT911          /* GT911 电容触控驱动 */

/* 协议标准配置宏  取消注释即开启整个项目的RTOS执行标准*/
/* 启动CMSIS v2 标准 */
#define OSAL_BACKEND_CMSIS_OS2

/* 裸机 和 RTOS环境配置宏 */
/* 1 使用 FreeRTOS 的taskENTER_CRITICAL()/FROM_ISR
 * 0 使用 PRIMASK  全关中断 （延时更大）
 */
#define OSAL_CRITICAL_IMPL_FREERTOS
#endif  // SMARTLOCK_CONFIG_CUS_H
