#include "osal.h"

#if OSAL_BACKEND_CMSIS_OS2
#include "cmsis_os2.h"
#include "cmsis_gcc.h" /*　＿＿get_IPSR */
#include "stddef.h"

#if OSAL_CRITICAL_IMPL_FREERTOS
#include "FreeRTOS.h"
#include "task.h"
#endif


/* ================================================ 内核状态/时间 ====================================================== */
/**
 *
 * @param timeout_ms 需要转换的超时时间
 * @return 将 ms 转化为 RTOS ticks
 */
static uint32_t OSAL_timeout_ms_to_kernel_ticks(uint32_t timeout_ms) {
    if (timeout_ms == OSAL_WAIT_FOREVER) return osWaitForever;
    if (timeout_ms == 0) return 0;
    return OSAL_ms_to_ticks(timeout_ms);
}

/**
 *
 * @return 返回当前调度器是否启动
 */
bool OSAL_kernel_is_running() {
    return (osKernelGetState() == osKernelRunning);
}

/**
 *
 * @return RTOS ticks
 */
osal_tick_t OSAL_tick_get() {
    return (uint32_t) osKernelGetTickCount();
}

/**
 *
 * @return RTOS 心跳频率
 */
uint32_t OSAL_tick_freq_hz() {
    return (uint32_t) osKernelGetTickFreq();
}

/**
 *
 * @param ms 需要转换的ms数
 * @return 返回 RTOS 心跳数
 */
uint32_t OSAL_ms_to_ticks(uint32_t ms) {
    if (ms == 0) return 0;
    const uint32_t hz = OSAL_tick_freq_hz();
    /* 向上取整 */
    const uint64_t nums = (uint64_t) ms * (uint64_t) hz + (999U);
    return (uint32_t) (nums / 1000U);
}

/**
 *
 * @param ticks 需要转换的 tick数
 * @return 返回需要的 ms单位
 */
uint32_t OSAL_tick_to_ms(osal_tick_t ticks) {
    const uint32_t hz = OSAL_tick_freq_hz();
    if (hz == 0) return 0;
    const uint64_t nums = (uint64_t) ticks * (uint64_t) 1000U;
    return (uint32_t) (nums / (uint64_t) hz);
}

/**
 *
 * @param ms 需要延时的ms 数
 * @return 返回结果
 */
ret_code_t OSAL_delay_ms(uint32_t ms) {
    if (ms == 0) return RET_OK;
    (void) osDelay(OSAL_ms_to_ticks(ms));
    return RET_OK;
}

/**
 *
 * @return 是否在中断中
 */
bool osal_in_isr(void) {
    return (__get_IPSR() != 0u);
}

/**
 *
 * @param start_tick 开始的 tick数
 * @param duration_ms 需要判断的超时的时间
 * @return 是否超时
 */
bool osal_is_timeout(osal_tick_t start_tick, uint32_t duration_ms) {
    const osal_tick_t now = OSAL_tick_get();
    const uint32_t dur_ticks = OSAL_ms_to_ticks(duration_ms);
    return ((osal_tick_t) (now - start_tick) >= (osal_tick_t) dur_ticks);
}

/* ====================================================== 临界区 ====================================================== */
/**
 * @note 根据宏定义的不同选择 关中断或者RTOS的临界区
 */
void OSAL_enter_critical(void) {
#if OSAL_CRITICAL_IMPL_FREERTOS
    taskENTER_CRITICAL();
#else
    __disable_irq();
    __DMB();
#endif
}

/**
 * @note 退出临界区
 */
void OSAL_exit_critical(void) {
#if OSAL_CRITICAL_IMPL_FREERTOS
    taskEXIT_CRITICAL();
#else
    __DMB();
    __enable_irq();
#endif
}

/**
 * @brief 进入临界区的中断版本
 * @param state 进入中断的变量
 */
void OSAL_enter_critical_from_isr(osal_crit_state_t *state) {
    if (!state) return;
#if OSAL_CRITICAL_IMPL_FREERTOS
    const UBaseType_t s = taskENTER_CRITICAL_FROM_ISR();
    *state = (osal_crit_state_t) s;
#else
    uint32_t s = __get_PRIMASK();
    __disable_irq();
    __DMB();
    *state = (osal_crit_state_t) s;
#endif
}

/**
 * @brief 退出临界区的中断版本
 * @param state 进入中断的变量
 */
void OSAL_exit_critical_from_isr(osal_crit_state_t state) {
#if OSAL_CRITICAL_IMPL_FREERTOS
    taskEXIT_CRITICAL_FROM_ISR((UBaseType_t)state);
#else
    __DMB();
    __set_PRIMASK((uint32_t) state);
#endif
}

/* ================================================== 互斥锁 ========================================================= */
/**
 * @brief 创建一个互斥锁
 * @param out 返回锁对象
 * @param name 锁名称
 * @param recursive
 * @param prio_inherit
 * @return 锁是否创建成功
 */
ret_code_t OSAL_mutex_create(osal_mutex_t *out, const char *name, bool recursive, bool prio_inherit) {
    if (!out) return RET_E_INVALID_ARG;

    osMutexAttr_t attr;
    attr.name = name;
    attr.attr_bits = 0;
    if (recursive) attr.attr_bits |= osMutexRecursive;
    if (prio_inherit)attr.attr_bits |= osMutexPrioInherit;
    attr.cb_mem = NULL;
    attr.cb_size = 0;

    osMutexId_t id = osMutexNew(&attr);
    if (!id) return RET_E_NO_MEM;

    *out = (osal_mutex_t) id;
    return RET_OK;
}

/**
 * @brief 获取一个互斥锁
 * @param mutex 锁对象
 * @param timeout_ms 超时时间
 * @return 锁是否获取成功
 */
ret_code_t OSAL_mutex_lock(osal_mutex_t mutex, uint32_t timeout_ms) {
    if (!mutex) return RET_E_INVALID_ARG;
    const uint32_t to = OSAL_timeout_ms_to_kernel_ticks(timeout_ms);
    const osStatus_t st = osMutexAcquire((osMutexId_t) mutex, to);
    if (st == osOK) return RET_OK;
    if (st == osErrorTimeout) return RET_E_TIMEOUT;
    return RET_E_FAIL;
}

/**
 *
 * @param mutex 锁对象
 * @return 锁是否释放成功
 */
ret_code_t OSAL_mutex_unlock(osal_mutex_t mutex) {
    if (!mutex) return RET_E_INVALID_ARG;
    const osStatus_t st = osMutexRelease((osMutexId_t) mutex);
    return (st == osOK) ? RET_OK : RET_E_FAIL;
}



#endif
