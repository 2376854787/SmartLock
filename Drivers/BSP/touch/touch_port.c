/**
 ****************************************************************************************************
 * @file        touch_port.c
 * @brief       Touch(CTP) RTOS port layer implementation
 *
 * 目的：
 *  - 本工程 CTP 使用软件 I2C(bit-bang GPIO)。在 RTOS 下若发生并发访问，I2C 时序会被打乱，
 *    典型表现为：触摸偶发失灵、I2C ACK 超时、甚至 SDA 被拉死。
 *  - 通过在一次 I2C 事务(start->stop)外层加互斥锁，保证同一时刻只有一个上下文操作软件 I2C。
 *  - ms 级 delay 在 RTOS 下使用 osDelay/vTaskDelay 让出 CPU；us 级 delay 保持忙等。
 ****************************************************************************************************
 */
#include "APP_config.h"
#include "touch_port.h"
#include "hal_time.h"
#if defined(TOUCH_OS_FREERTOS)
static SemaphoreHandle_t s_tp_mutex = NULL;
#elif defined(TOUCH_OS_CMSIS_OS2)
static osMutexId_t s_tp_mutex = NULL;
#elif defined(TOUCH_OS_UCOSII)
/* uC/OS-II 的互斥体类型在不同移植里可能不同：
 *  - 经典 uC/OS-II：OS_EVENT* + OSMutexCreate/Pend/Post
 *  - 某些工程可能是 uC/OS-III：OS_MUTEX 结构体
 *
 * 这里给出 uC/OS-II 经典接口的实现。如果你的 os.h 不是该接口，请按你的 RTOS 封装替换。
 */
static OS_EVENT *s_tp_mutex = (OS_EVENT *)0;
#endif

void touch_port_init(void)
{
#if defined(TOUCH_OS_FREERTOS)
    if (s_tp_mutex == NULL)
    {
        s_tp_mutex = xSemaphoreCreateMutex();
    }
#elif defined(TOUCH_OS_CMSIS_OS2)
    if (s_tp_mutex == NULL)
    {
        const osMutexAttr_t attr = {
            .name = "tp_i2c_mutex"
        };
        s_tp_mutex = osMutexNew(&attr);
    }
#elif defined(TOUCH_OS_UCOSII)
    if (s_tp_mutex == (OS_EVENT *)0)
    {
        INT8U err;
        s_tp_mutex = OSMutexCreate(10u, &err);
        (void)err;
    }
#else
    /* bare-metal: no-op */
#endif
}

void touch_port_lock(void)
{
#if defined(TOUCH_OS_FREERTOS)
    if (s_tp_mutex != NULL)
    {
        /* 调度器未启动时不允许阻塞 */
        if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        {
            (void)xSemaphoreTake(s_tp_mutex, portMAX_DELAY);
        }
    }
#elif defined(TOUCH_OS_CMSIS_OS2)
    if (s_tp_mutex != NULL)
    {
        if (osKernelGetState() == osKernelRunning)
        {
            (void)osMutexAcquire(s_tp_mutex, osWaitForever);
        }
    }
#elif defined(TOUCH_OS_UCOSII)
    if (s_tp_mutex != (OS_EVENT *)0)
    {
        INT8U err;
        /* 0 表示永久等待 */
        (void)OSMutexPend(s_tp_mutex, 0u, &err);
        (void)err;
    }
#else
    /* bare-metal: no-op */
#endif
}

void touch_port_unlock(void)
{
#if defined(TOUCH_OS_FREERTOS)
    if (s_tp_mutex != NULL)
    {
        if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        {
            (void)xSemaphoreGive(s_tp_mutex);
        }
    }
#elif defined(TOUCH_OS_CMSIS_OS2)
    if (s_tp_mutex != NULL)
    {
        if (osKernelGetState() == osKernelRunning)
        {
            (void)osMutexRelease(s_tp_mutex);
        }
    }
#elif defined(TOUCH_OS_UCOSII)
    if (s_tp_mutex != (OS_EVENT *)0)
    {
        INT8U err;
        err = OSMutexPost(s_tp_mutex);
        (void)err;
    }
#else
    /* bare-metal: no-op */
#endif
}

void touch_port_delay_ms(uint32_t ms)
{
#if defined(TOUCH_OS_FREERTOS)
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        vTaskDelay(pdMS_TO_TICKS(ms));
        return;
    }
#elif defined(TOUCH_OS_CMSIS_OS2)
    if (osKernelGetState() == osKernelRunning)
    {
        osDelay(ms);
        return;
    }
#elif defined(TOUCH_OS_UCOSII)
    /* 经典 uC/OS-II: ticks 延时。这里按 1ms=1tick 处理；
     * 若你的 OS_TICKS_PER_SEC != 1000，请按你的工程改算。
     */
    if (ms > 0)
    {
        (void)OSTimeDly(ms);
        return;
    }
#endif

    /* scheduler not running / bare-metal fallback */
    delay_ms(ms);
}

uint32_t touch_port_get_tick_ms(void)
{
#if defined(TOUCH_OS_FREERTOS)
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        TickType_t t = xTaskGetTickCount();
        return (uint32_t)(t * portTICK_PERIOD_MS);
    }
#elif defined(TOUCH_OS_CMSIS_OS2)
    if (osKernelGetState() == osKernelRunning)
    {
        return (uint32_t)osKernelGetTickCount();
    }
#elif defined(TOUCH_OS_UCOSII)
    return (uint32_t)OSTimeGet();
#endif

    /* bare-metal */
    return (uint32_t) hal_get_tick_ms();
}
