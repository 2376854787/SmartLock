#ifndef SMARTLOCK_PRODUCT_SMARTCLOCK_H
#define SMARTLOCK_PRODUCT_SMARTCLOCK_H

/* ============================================================================
 * L2 产品层配置（Product）
 * 负责“产品功能策略”：模块开关、运行策略开关。
 * ============================================================================
 */

/* 功能开启宏（1 开启 / 0 关闭） */
/* 功能开启宏  取消注释即开启整个项目的 指定功能 */
#define CFG_FEAT_LOG_SYSTEM        1 /* 日志管理 */
#define CFG_FEAT_AT_SYSTEM         1 /* AT框架管理 */
#define CFG_FEAT_ASSERT_SYSTEM     1 /* 断言管理 */
#define CFG_FEAT_STATIC_ALLOCATION 1 /* 静态内存分配 */
#define CFG_FEAT_RINGBUFFER_SYSTEM 1 /* 环形缓冲区 */
#define CFG_FEAT_HFSM_SYSTEM       1 /* HFSM系统 */
#define CFG_FEAT_KEYS              1 /* 使能按键 */
#define CFG_FEAT_CRC16             1 /* 启动CRC16 */
#define CFG_FEAT_MEMORY_POOL       1 /* 启动内存池 */

/* 自定义实现的 HAL 层功能启用 */
#define CFG_FEAT_HAL_WDG        1 /* 启动看门狗 */
#define CFG_FEAT_WDG_SUPERVISOR 1 /* 启动支持 */
#define CFG_FEAT_HAL_GPIO       1 /* 启动GPIO */
#define CFG_FEAT_HAL_UART       1 /* 启动串口 */
#define CFG_FEAT_HAL_TIME       1 /* 启动time */
#define CFG_FEAT_SOFT_I2C       1 /* 软件 I2C && CFG_FEAT_HAL_GPIO */
#define CFG_FEAT_GT911          1 /* GT911 电容触控驱动 */
#define CFG_FEAT_HAL_SPI        1 /* 启动SPI抽象 */
#define CFG_FEAT_HAL_I2C        1 /* 启动 I2C 抽象 */
#define CFG_FEAT_HAL_ERROR_CATCH 0 /* 统一 HAL 错误钩子覆盖(1: 启用外部覆盖) */

/* HAL SPI 运行策略参数 */
#define CFG_PARAM_SPI_LOG_PORT_ERR        1 /* port->HAL 映射错误是否记录日志 */
#define CFG_PARAM_SPI_LOG_PORT_ERR_IN_ISR 0 /* 1: ISR中也打日志(谨慎) */
/* HAL UART 运行策略参数 */
#define CFG_PARAM_UART_LOG_PORT_ERR        1 /* port->HAL 映射错误是否记录日志 */
#define CFG_PARAM_UART_LOG_PORT_ERR_IN_ISR 0 /* 1: ISR中也打日志(谨慎) */
/* HAL GPIO 运行策略参数 */
#define CFG_PARAM_GPIO_LOG_PORT_ERR        1 /* port->HAL 映射错误是否记录日志 */
#define CFG_PARAM_GPIO_LOG_PORT_ERR_IN_ISR 0 /* 1: ISR中也打日志(谨慎) */
/* HAL WDG 运行策略参数 */
#define CFG_PARAM_WDG_LOG_PORT_ERR        1 /* port->HAL 映射错误是否记录日志 */
#define CFG_PARAM_WDG_LOG_PORT_ERR_IN_ISR 0 /* 1: ISR中也打日志(谨慎) */

/* 协议标准配置宏  取消注释即开启整个项目的RTOS执行标准 */
/* 启动CMSIS v2 标准 */
#define CFG_FEAT_OSAL_BACKEND_CMSIS_OS2 1

/* 裸机 和 RTOS环境配置宏 */
/* 1 使用 FreeRTOS 的taskENTER_CRITICAL()/FROM_ISR
 * 0 使用 PRIMASK  全关中断 （延时更大）
 */
#define CFG_FEAT_OSAL_CRITICAL_FREERTOS 1

#endif /* SMARTLOCK_PRODUCT_SMARTCLOCK_H */
