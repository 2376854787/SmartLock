//
// Created by yan on 2025/12/20.
//

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
#define ENABLE_HAL_GPIO
#define ENABLE_HAL_UART
#define ENABLE_HAL_TIME

/* 协议标准配置宏  取消注释即开启整个项目的RTOS执行标准*/
/* 启动CMSIS v2 标准 */
#define OSAL_BACKEND_CMSIS_OS2 1

/* 裸机 和 RTOS环境配置宏 */
/* 1 使用 FreeRTOS 的taskENTER_CRITICAL()/FROM_ISR
 * 0 使用 PRIMASK  全关中断 （延时更大）
 */
#define OSAL_CRITICAL_IMPL_FREERTOS 1
#endif  // SMARTLOCK_CONFIG_CUS_H
