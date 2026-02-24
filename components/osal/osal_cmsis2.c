#include "APP_config.h"
#include "osal.h"
#include "osal_config.h"

#if (defined(CFG_FEAT_OSAL_BACKEND_CMSIS_OS2) && (CFG_FEAT_OSAL_BACKEND_CMSIS_OS2 == 1))
#include "cmsis_gcc.h" /* */
#include "cmsis_os.h"
#include "cmsis_os2.h"
#include "hal_time.h"
#include "stddef.h"
/* ==============最大关中断时间记录 =================*/
#ifndef OSAL_CRITMON_THRESHOLD_US
#define OSAL_CRITMON_THRESHOLD_US 50u
#endif

static uint32_t g_critmon_start_cyc     = 0u;
static uint32_t g_critmon_cycles_per_us = 0u;
static uint32_t g_critmon_nest          = 0u;
static uint32_t g_critmon_enter_pc      = 0u;

/* 默认弱实现：上层可在 port/app 侧覆盖为 blackbox 或其他观测上报 */
__attribute__((weak)) void OSAL_on_critical_duration_us(uint32_t dur_us) {
    (void)dur_us;
}

static inline uint32_t read_lr_return_addr(void) {
#if defined(__GNUC__) || defined(__clang__)
    return (uint32_t)(uintptr_t)__builtin_return_address(0);
#else
    return 0u;
#endif
}

/* ==============最大关中断时间记录 =================*/
/* ============ 断言 =============*/
/* 断言系统全局配置 */
#if (defined(CFG_FEAT_ASSERT_SYSTEM) && (CFG_FEAT_ASSERT_SYSTEM == 1))
#include "assert_cus.h"
#endif
#if (defined(CFG_FEAT_ASSERT_SYSTEM) && (CFG_FEAT_ASSERT_SYSTEM == 1))
#define OSAL_ASSERT(expr) ASSERT_PARAM(expr)
#define OSAL_FAULT(expr)  ASSERT_FATAL(expr)
#else
#define OSAL_ASSERT(expr) \
    do {                  \
        (void)(expr);     \
    } while (0)
#define OSAL_FAULT(expr) \
    do {                 \
        (void)(expr);    \
    } while (0)
#endif
/* ============ 断言 =============*/

#if (defined(CFG_FEAT_OSAL_CRITICAL_FREERTOS) && (CFG_FEAT_OSAL_CRITICAL_FREERTOS == 1))
#include "FreeRTOS.h"
#include "task.h"
#endif

/* ====== 临界区模式编码：使用 state 高位标记，低位存原始值 ====== */
#define OSAL_CRIT_MODE_MASK        (0xC0000000UL)
#define OSAL_CRIT_MODE_PRIMASK     (0x40000000UL) /* 线程态&调度未启：PRIMASK 可恢复 */
#define OSAL_CRIT_MODE_RTOS_THREAD (0x80000000UL) /* 线程态&调度已启：taskENTER/EXIT_CRITICAL */
#define OSAL_CRIT_MODE_RTOS_ISR    (0xC0000000UL) /* ISR：taskENTER/EXIT_CRITICAL_FROM_ISR */

#define OSAL_CRIT_PAYLOAD_MASK (0x3FFFFFFFUL) /* 低 30 位存 payload（足够容纳常见返回值） */
#define OSAL_MUTEX_RET(clas_, err_) \
    RET_MAKE(RET_MOD_OSAL, RET_SUB_OSAL_MUTEX, RET_CODE_MAKE((clas_), (err_)))
#define OSAL_TASK_RET(clas_, err_) \
    RET_MAKE(RET_MOD_OSAL, RET_SUB_OSAL_TASK, RET_CODE_MAKE((clas_), (err_)))
#define OSAL_QUEUE_RET(clas_, err_) \
    RET_MAKE(RET_MOD_OSAL, RET_SUB_OSAL_QUEUE, RET_CODE_MAKE((clas_), (err_)))
#define OSAL_SEM_RET(clas_, err_) \
    RET_MAKE(RET_MOD_OSAL, RET_SUB_OSAL_SEM, RET_CODE_MAKE((clas_), (err_)))
#define OSAL_TIMER_RET(clas_, err_) \
    RET_MAKE(RET_MOD_OSAL, RET_SUB_OSAL_TIMER, RET_CODE_MAKE((clas_), (err_)))
#define OSAL_ISR_RET(clas_, err_) \
    RET_MAKE(RET_MOD_OSAL, RET_SUB_OSAL_ISR, RET_CODE_MAKE((clas_), (err_)))

/* ================================================ 内核状态/时间
 * ====================================================== */
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
 * @brief 将自定义优先级转换为CMSIS-RTOS2的对应优先级
 * @param p 优先级映射转换
 * @return
 */
static osPriority_t OSAL_map_prio(osal_priority_t p) {
    switch (p) {
        case OSAL_PRIO_LOW:
            return osPriorityLow;
        case OSAL_PRIO_NORMAL:
            return osPriorityNormal;
        case OSAL_PRIO_HIGH:
            return osPriorityHigh;
        case OSAL_PRIO_REALTIME:
            return osPriorityRealtime;
        default:
            return osPriorityNormal;
    }
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
    return (uint32_t)osKernelGetTickCount();
}

/**
 *
 * @return RTOS 心跳频率
 */
uint32_t OSAL_tick_freq_hz() {
    return (uint32_t)osKernelGetTickFreq();
}

/**
 * @brief 根据系统心跳精准转换为 心跳数
 * @param ms 需要转换的ms数
 * @return 返回 RTOS 心跳数
 */
uint32_t OSAL_ms_to_ticks(uint32_t ms) {
    if (ms == 0) return 0;
    const uint32_t hz   = OSAL_tick_freq_hz();
    /* 向上取整 */
    const uint64_t nums = (uint64_t)ms * (uint64_t)hz + (999U);
    return (uint32_t)(nums / 1000U);
}

/**
 * @brief 根据系统心跳精准转换为ms
 * @param ticks 需要转换的 tick数
 * @return 返回需要的 ms单位
 */
uint32_t OSAL_tick_to_ms(osal_tick_t ticks) {
    const uint32_t hz = OSAL_tick_freq_hz();
    if (hz == 0) return 0;
    const uint64_t nums = (uint64_t)ticks * (uint64_t)1000U;
    return (uint32_t)(nums / (uint64_t)hz);
}

/**
 * @brief 根据系统心跳频率精准延时
 * @param ms 需要延时的ms 数
 * @return 返回结果
 */
ret_code_t OSAL_delay_ms(uint32_t ms) {
    if (ms == 0) return RET_OK;
    const osStatus_t st = osDelay(OSAL_ms_to_ticks(ms));
    if (st == osOK) return RET_OK;
    if (st == osErrorISR) return OSAL_TASK_RET(RET_CLASS_FATAL, RET_R_ISR_CONTEXT);
    if (st == osErrorParameter) return OSAL_TASK_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    return OSAL_TASK_RET(RET_CLASS_FATAL, RET_R_PANIC);
}

/**
 * @brief 获取当前环境
 * @return 是否在中断中
 */
bool OSAL_in_isr(void) {
    return (__get_IPSR() != 0u);
}

/**
 *
 * @param start_tick 开始的 tick数
 * @param duration_ms 需要判断的超时的时间
 * @return 是否超时
 */
bool OSAL_is_timeout(osal_tick_t start_tick, uint32_t duration_ms) {
    const osal_tick_t now    = OSAL_tick_get();
    const uint32_t dur_ticks = OSAL_ms_to_ticks(duration_ms);
    return ((osal_tick_t)(now - start_tick) >= (osal_tick_t)dur_ticks);
}

/* ====================================================== 临界区
 * ====================================================== */
/* 裸机/调度未启动：PRIMASK 嵌套 */
static uint32_t g_irq_crit_nest     = 0U;
static uint32_t g_irq_saved_primask = 0U;

/**
 * @brief 获取进入时的时间戳 和 lr寄存器值
 */
static inline void OSAL_CritMon_Enter(uint32_t enter_pc) {
    if (g_critmon_nest == 0u) {
        /* 首次获取一下每 us的周期 */
        if (g_critmon_cycles_per_us == 0u) {
            g_critmon_cycles_per_us = hal_get_cycles_per_us(); /* 0 表示 DWT 未就绪 */
        }
        /* 1、记录开始时的周期数 */
        g_critmon_start_cyc = hal_get_cycle32();
        /* 记录pc */
        g_critmon_enter_pc  = enter_pc; /* 记录 OSAL_enter_critical 的调用点 */
    }
    g_critmon_nest++;
}

/**
 *
 * @param out_dur_us
 * @param out_enter_pc
 */
static inline void OSAL_CritMon_Exit(uint32_t* out_dur_us, uint32_t* out_enter_pc) {
    OSAL_ASSERT((out_dur_us != NULL) && (out_enter_pc != NULL));
    if (!out_dur_us || !out_enter_pc) return;
    /* 初始化为0防止垃圾信息 */
    *out_dur_us   = 0u;
    *out_enter_pc = 0u;

    /* 防御：调用不成对时不炸 OSAL，自行让 OSAL_FAULT 去管 */
    if (g_critmon_nest == 0u) return;
    /* 直到嵌套的循环完成 */
    if (g_critmon_nest == 1u) {
        *out_enter_pc = g_critmon_enter_pc;

        /* DWT 未初始化/不可用：不计时（避免 ms*1000 的 1000us 假峰值） */
        if (g_critmon_cycles_per_us != 0u) {
            const uint32_t now_cyc   = hal_get_cycle32();
            const uint32_t delta_cyc = (uint32_t)(now_cyc - g_critmon_start_cyc); /* wrap-safe */
            *out_dur_us              = (uint32_t)(delta_cyc / g_critmon_cycles_per_us);
        } else {
            *out_dur_us = 0u;
        }
    }

    g_critmon_nest--;
}

/**
 * @note 根据宏定义的不同选择 关中断或者RTOS的临界区
 */
void OSAL_enter_critical(void) {
    OSAL_FAULT(!OSAL_in_isr());
    /* 先取调用点 PC（此时还没关中断，取 return address 才是调用者） */
    const uint32_t enter_pc = read_lr_return_addr();
#if (defined(CFG_FEAT_OSAL_CRITICAL_FREERTOS) && (CFG_FEAT_OSAL_CRITICAL_FREERTOS == 1))
    if (OSAL_kernel_is_running()) {
        taskENTER_CRITICAL();         /* 先真正屏蔽 */
        OSAL_CritMon_Enter(enter_pc); /* 再开始计时：避免被 SysTick 抢占导致 ~1000us 假峰值 */
        return;
    }
#endif
    /* 调度未启动/裸机：PRIMASK 嵌套 */
    if (g_irq_crit_nest == 0U) {
        g_irq_saved_primask = __get_PRIMASK();
        __disable_irq();
        __DMB();
    }
    g_irq_crit_nest++;

    OSAL_CritMon_Enter(enter_pc);
}

/**
 * @note 退出临界区
 */
void OSAL_exit_critical(void) {
    OSAL_FAULT(!OSAL_in_isr());
    uint32_t dur_us   = 0u;
    uint32_t enter_pc = 0u;
#if (defined(CFG_FEAT_OSAL_CRITICAL_FREERTOS) && (CFG_FEAT_OSAL_CRITICAL_FREERTOS == 1))
    if (OSAL_kernel_is_running()) {
        OSAL_CritMon_Exit(&dur_us, &enter_pc);
        taskEXIT_CRITICAL();
        if (dur_us != 0u) {
            OSAL_on_critical_duration_us(dur_us);
            OSAL_FAULT(dur_us <= OSAL_CRITMON_THRESHOLD_US);
        }
        return;
    }
#endif
    /* PRIMASK 嵌套恢复 */
    OSAL_FAULT(g_irq_crit_nest > 0U);
    g_irq_crit_nest--;
    if (g_irq_crit_nest == 0U) {
        OSAL_CritMon_Exit(&dur_us, &enter_pc);
        __DMB();
        __set_PRIMASK(g_irq_saved_primask);
        if (dur_us != 0u) {
            OSAL_on_critical_duration_us(dur_us);
            OSAL_FAULT(dur_us <= OSAL_CRITMON_THRESHOLD_US);
        }
        return;
    }
    OSAL_CritMon_Exit(&dur_us, &enter_pc);
}

/**
 * @brief 进入临界区（可恢复版本）
 * @param state 保存进入前状态/模式
 */
void OSAL_enter_critical_ex(osal_crit_state_t* state) {
    OSAL_ASSERT(state != NULL);
    if (!state) return;
    const uint32_t enter_pc = read_lr_return_addr();
#if (defined(CFG_FEAT_OSAL_CRITICAL_FREERTOS) && (CFG_FEAT_OSAL_CRITICAL_FREERTOS == 1))
    if (OSAL_in_isr()) {
        if (OSAL_kernel_is_running()) {
            const UBaseType_t s = taskENTER_CRITICAL_FROM_ISR();
            *state              = (osal_crit_state_t)((((uint32_t)s) & OSAL_CRIT_PAYLOAD_MASK) |
                                         OSAL_CRIT_MODE_RTOS_ISR);
            OSAL_CritMon_Enter(enter_pc);
            return;
        }
    } else {
        if (OSAL_kernel_is_running()) {
            taskENTER_CRITICAL();
            *state = (osal_crit_state_t)OSAL_CRIT_MODE_RTOS_THREAD;
            OSAL_CritMon_Enter(enter_pc);
            return;
        }
    }
#endif
    /* PRIMASK */
    {
        const uint32_t s = __get_PRIMASK();
        __disable_irq();
        __DMB();
        *state = (osal_crit_state_t)((s & 0x1UL) | OSAL_CRIT_MODE_PRIMASK);
    }
    OSAL_CritMon_Enter(enter_pc);
}

/**
 * @brief 退出临界区（可恢复版本）
 * @param state 进入临界区时保存的状态
 */
void OSAL_exit_critical_ex(osal_crit_state_t state) {
    const uint32_t mode = ((uint32_t)state) & OSAL_CRIT_MODE_MASK;
    uint32_t dur_us     = 0u;
    uint32_t enter_pc   = 0u;
#if (defined(CFG_FEAT_OSAL_CRITICAL_FREERTOS) && (CFG_FEAT_OSAL_CRITICAL_FREERTOS == 1))
    if (mode == OSAL_CRIT_MODE_RTOS_ISR) {
        const UBaseType_t s = (UBaseType_t)(((uint32_t)state) & OSAL_CRIT_PAYLOAD_MASK);
        OSAL_CritMon_Exit(&dur_us, &enter_pc);
        taskEXIT_CRITICAL_FROM_ISR(s);
        if (dur_us != 0u) {
            OSAL_on_critical_duration_us(dur_us);
            OSAL_FAULT(dur_us <= OSAL_CRITMON_THRESHOLD_US);
        }
        return;
    }

    if (mode == OSAL_CRIT_MODE_RTOS_THREAD) {
        OSAL_CritMon_Exit(&dur_us, &enter_pc);
        taskEXIT_CRITICAL();
        if (dur_us != 0u) {
            OSAL_on_critical_duration_us(dur_us);
            OSAL_FAULT(dur_us <= OSAL_CRITMON_THRESHOLD_US);
        }
        return;
    }
#endif

    /* PRIMASK 模式 */
    if (mode == OSAL_CRIT_MODE_PRIMASK) {
        OSAL_CritMon_Exit(&dur_us, &enter_pc);
        __DMB();
        __set_PRIMASK(((uint32_t)state) & 0x1UL);
        if (dur_us != 0u) {
            OSAL_on_critical_duration_us(dur_us);
            OSAL_FAULT(dur_us <= OSAL_CRITMON_THRESHOLD_US);
        }
        return;
    }
    /* 断言 */
    OSAL_FAULT(0);
}

/**
 * @brief 进入临界区的中断版本
 * @param state 进入中断的变量
 */
void OSAL_enter_critical_from_isr(osal_crit_state_t* state) {
    OSAL_ASSERT(state != NULL);
    if (!state) return;
    const uint32_t enter_pc = read_lr_return_addr();
#if (defined(CFG_FEAT_OSAL_CRITICAL_FREERTOS) && (CFG_FEAT_OSAL_CRITICAL_FREERTOS == 1))
    if (OSAL_kernel_is_running()) {
        const UBaseType_t s = taskENTER_CRITICAL_FROM_ISR();
        *state              = (osal_crit_state_t)s;
        OSAL_CritMon_Enter(enter_pc);
        return;
    }
#endif

    {
        const uint32_t s = __get_PRIMASK();
        __disable_irq();
        __DMB();
        *state = (osal_crit_state_t)s;
    }
    OSAL_CritMon_Enter(enter_pc);
}

/**
 * @brief 退出临界区的中断版本
 * @param state 进入中断的变量
 */
void OSAL_exit_critical_from_isr(osal_crit_state_t state) {
    uint32_t dur_us   = 0u;
    uint32_t enter_pc = 0u;
#if (defined(CFG_FEAT_OSAL_CRITICAL_FREERTOS) && (CFG_FEAT_OSAL_CRITICAL_FREERTOS == 1))
    if (OSAL_kernel_is_running()) {
        OSAL_CritMon_Exit(&dur_us, &enter_pc);
        taskEXIT_CRITICAL_FROM_ISR((UBaseType_t)state);
        if (dur_us != 0u) {
            OSAL_on_critical_duration_us(dur_us);
            OSAL_FAULT(dur_us <= OSAL_CRITMON_THRESHOLD_US);
        }
        return;
    }
#endif

    OSAL_CritMon_Exit(&dur_us, &enter_pc);
    __DMB();
    __set_PRIMASK((uint32_t)state);
    if (dur_us != 0u) {
        OSAL_on_critical_duration_us(dur_us);
        OSAL_FAULT(dur_us <= OSAL_CRITMON_THRESHOLD_US);
    }
}

/* ================================================== 互斥锁
 * ========================================================= */
/**
 * @brief 创建一个互斥锁
 * @param out 返回锁对象
 * @param name 锁名称
 * @param recursive 优先级反转
 * @param prio_inherit 优先级继承
 * @return 锁是否创建成功
 */
ret_code_t OSAL_mutex_create(osal_mutex_t* out, const char* name, bool recursive,
                             bool prio_inherit) {
    OSAL_ASSERT(out != NULL);
    if (!out) return OSAL_MUTEX_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);

    osMutexAttr_t attr;
    attr.name      = name;
    attr.attr_bits = 0;
    if (recursive) attr.attr_bits |= osMutexRecursive;
    if (prio_inherit) attr.attr_bits |= osMutexPrioInherit;
    attr.cb_mem          = NULL;
    attr.cb_size         = 0;
    const osMutexId_t id = osMutexNew(&attr);
    if (!id) return OSAL_MUTEX_RET(RET_CLASS_RESOURCE, RET_R_NO_MEM);
    *out = (osal_mutex_t)id;
    return RET_OK;
}

/**
 * @brief 删除互斥锁
 * @param mutex 互斥锁指针
 * @return 返回删除结果
 */
ret_code_t OSAL_mutex_delete(osal_mutex_t mutex) {
    OSAL_ASSERT(mutex != NULL);
    if (!mutex) return OSAL_MUTEX_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    const osStatus_t status = osMutexDelete((osMutexId_t)mutex);
    if (status == osOK) return RET_OK;
    if (status == osErrorParameter) return OSAL_MUTEX_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    return OSAL_MUTEX_RET(RET_CLASS_FATAL, RET_R_PANIC);
}

/**
 * @brief 获取一个互斥锁
 * @param mutex 锁对象
 * @param timeout_ms 超时时间
 * @return 锁是否获取成功
 */
ret_code_t OSAL_mutex_lock(osal_mutex_t mutex, uint32_t timeout_ms) {
    OSAL_ASSERT(mutex != NULL);
    if (!mutex) return OSAL_MUTEX_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    const uint32_t to   = OSAL_timeout_ms_to_kernel_ticks(timeout_ms);
    const osStatus_t st = osMutexAcquire((osMutexId_t)mutex, to);
    if (st == osOK) return RET_OK;
    if (st == osErrorTimeout) return OSAL_MUTEX_RET(RET_CLASS_TIMEOUT, RET_R_TIMEOUT);
    return OSAL_MUTEX_RET(RET_CLASS_FATAL, RET_R_PANIC);
}

/**
 * @brief 互斥锁解锁
 * @param mutex 锁对象
 * @return 锁是否释放成功
 */
ret_code_t OSAL_mutex_unlock(osal_mutex_t mutex) {
    OSAL_ASSERT(mutex != NULL);
    if (!mutex) return OSAL_MUTEX_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    const osStatus_t st = osMutexRelease((osMutexId_t)mutex);
    return (st == osOK) ? RET_OK : OSAL_MUTEX_RET(RET_CLASS_FATAL, RET_R_PANIC);
}

/* ================================================== 信号量
 * ========================================================= */
/**
 * @brief 初始化信号量
 * @param out 存储创建信号量返回的ID
 * @param name 信号量名称
 * @param initial_count 初始数量
 * @param max_count  最大数量
 * @return 返回创建结果
 */
ret_code_t OSAL_sem_create(osal_sem_t* out, const char* name, uint32_t initial_count,
                           uint32_t max_count) {
    OSAL_ASSERT((out != NULL) && (max_count != 0u));
    if (!out || max_count == 0) return OSAL_SEM_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    osSemaphoreAttr_t attr;
    attr.name          = name;
    attr.cb_mem        = NULL;
    attr.cb_size       = 0;
    attr.attr_bits     = 0;
    osSemaphoreId_t id = osSemaphoreNew(max_count, initial_count, &attr);
    if (!id) return OSAL_SEM_RET(RET_CLASS_RESOURCE, RET_R_NO_MEM);
    *out = (osal_sem_t)id;
    return RET_OK;
}

/**
 * @brief 删除信号量
 * @param sem 信号量指针
 * @return 返回删除结果
 */
ret_code_t OSAL_sem_delete(osal_sem_t sem) {
    OSAL_ASSERT(sem != NULL);
    if (!sem) return OSAL_SEM_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    const osStatus_t status = osSemaphoreDelete((osSemaphoreId_t)sem);
    return (status == osOK) ? RET_OK : OSAL_SEM_RET(RET_CLASS_FATAL, RET_R_PANIC);
}

/**
 * @brief 获取信号量
 * @param sem 信号量指针
 * @param timeout_ms 超时时间
 * @return 信号量获取结果
 * @note 中断中可以使用
 */
ret_code_t OSAL_sem_take(osal_sem_t sem, uint32_t timeout_ms) {
    OSAL_ASSERT(sem != NULL);
    if (!sem) return OSAL_SEM_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    const uint32_t to   = OSAL_timeout_ms_to_kernel_ticks(timeout_ms);
    const osStatus_t st = osSemaphoreAcquire((osSemaphoreId_t)sem, to);
    if (st == osOK) return RET_OK;
    if (st == osErrorTimeout) return OSAL_SEM_RET(RET_CLASS_TIMEOUT, RET_R_TIMEOUT);
    return OSAL_SEM_RET(RET_CLASS_FATAL, RET_R_PANIC);
}

/**
 * @brief 释放信号量
 * @param sem 信号量指针
 * @return 信号量释放是否成功
 * @note 中断中可以使用
 */
ret_code_t OSAL_sem_give(osal_sem_t sem) {
    OSAL_ASSERT(sem != NULL);
    if (!sem) return OSAL_SEM_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    const osStatus_t st = osSemaphoreRelease((osSemaphoreId_t)sem);
    if (st == osOK) return RET_OK;
    if (st == osErrorParameter) return OSAL_SEM_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    return OSAL_SEM_RET(RET_CLASS_FATAL, RET_R_PANIC);
}

/**
 * @brief 中断版本的信号量释放
 * @param sem 信号量指针
 * @return 释放信号量是否成功
 */
ret_code_t OSAL_sem_give_from_isr(osal_sem_t sem) {
    /* CMSIS-RTOS2 osSemaphoreRelease isr中可用 */
    return OSAL_sem_give(sem);
}
/* ================================================== 消息队列
 * ========================================================= */
/**
 * @brief 创建一个消息队列
 * @param out 存储消息队列的指针
 * @param name 名字
 * @param item_size 消息的大小
 * @param item_count 消息数量
 * @return消息队列创建是否成功
 * @norte 不可 ISR 中使用
 */
ret_code_t OSAL_msgq_create(osal_msgq_t* out, const char* name, uint32_t item_size,
                            uint32_t item_count) {
    OSAL_ASSERT((out != NULL) && (item_count != 0u) && (item_size != 0u));
    if (!out || item_count == 0 || item_size == 0)
        return OSAL_QUEUE_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    osMessageQueueAttr_t attr;
    attr.name             = name;
    attr.cb_mem           = NULL;
    attr.attr_bits        = 0;
    attr.mq_mem           = NULL;
    attr.cb_size          = 0;
    attr.mq_size          = 0;
    osMessageQueueId_t id = osMessageQueueNew(item_count, item_size, &attr);
    if (!id) return OSAL_QUEUE_RET(RET_CLASS_RESOURCE, RET_R_NO_MEM);
    *out = (osal_msgq_t)id;
    return RET_OK;
}

/**
 * @brief 删除消息队列
 * @param msgq 消息队列指针
 * @return 消息队列是否删除成功
 * @note IS_ISR 不可使用
 */
ret_code_t OSAL_msgq_delete(osal_msgq_t msgq) {
    OSAL_ASSERT(msgq != NULL);
    if (!msgq) return OSAL_QUEUE_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    const osStatus_t st = osMessageQueueDelete((osMessageQueueId_t)msgq);
    if (st == osOK) return RET_OK;
    if (st == osErrorParameter) return OSAL_QUEUE_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    if (st == osErrorISR) return OSAL_QUEUE_RET(RET_CLASS_FATAL, RET_R_ISR_CONTEXT);
    return OSAL_QUEUE_RET(RET_CLASS_FATAL, RET_R_PANIC);
}

/**
 * @brief 通过消息队列发送消息
 * @param msgq 消息队列
 * @param msg  需要放置的消息
 * @param timeout_ms 超时时间
 * @return 是否发送成功
 * @note 可以中断使用
 */
ret_code_t OSAL_msgq_put(osal_msgq_t msgq, void* msg, uint32_t timeout_ms) {
    OSAL_ASSERT((msgq != NULL) && (msg != NULL));
    if (!msgq || !msg) return OSAL_QUEUE_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    const uint32_t to   = OSAL_timeout_ms_to_kernel_ticks(timeout_ms);
    const osStatus_t st = osMessageQueuePut((osMessageQueueId_t)msgq, msg, 0, to);
    switch (st) {
        case osOK:
            return RET_OK;
        case osErrorTimeout:
            return OSAL_QUEUE_RET(RET_CLASS_TIMEOUT, RET_R_TIMEOUT);
        case osErrorISR:
            return OSAL_QUEUE_RET(RET_CLASS_FATAL, RET_R_ISR_CONTEXT);
        case osErrorParameter:
            return OSAL_QUEUE_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
        default:
            return OSAL_QUEUE_RET(RET_CLASS_FATAL, RET_R_PANIC);
    }
}

/**
 * @brief 指定时间内从消息队列获取消息
 * @param msgq 消息队列
 * @param msg  存放消息的地址
 * @param timeout_ms 超时时间
 * @return 是否获取成功
 * @note CMSIS-RTOS2 底层可从ISR执行
 */
ret_code_t OSAL_msgq_get(osal_msgq_t msgq, void* msg, uint32_t timeout_ms) {
    OSAL_ASSERT((msgq != NULL) && (msg != NULL));
    if (!msgq || !msg) return OSAL_QUEUE_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    const uint32_t to   = OSAL_timeout_ms_to_kernel_ticks(timeout_ms);
    const osStatus_t st = osMessageQueueGet((osMessageQueueId_t)msgq, msg, NULL, to);
    switch (st) {
        case osOK:
            return RET_OK;
        case osErrorTimeout:
            return OSAL_QUEUE_RET(RET_CLASS_TIMEOUT, RET_R_TIMEOUT);
        case osErrorISR:
            return OSAL_QUEUE_RET(RET_CLASS_FATAL, RET_R_ISR_CONTEXT);
        case osErrorParameter:
            return OSAL_QUEUE_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
        default:
            return OSAL_QUEUE_RET(RET_CLASS_FATAL, RET_R_PANIC);
    }
}

/* ============================================ 线程 && Flags
 * ====================================================== */
/**
 * @brief 创建一个线程
 * @param out 线程指针
 * @param fn  绑定的函数指针
 * @param arg 传递的参数
 * @param attr 需要传递的部分参数
 * @return 线程是否创建成功
 */
ret_code_t OSAL_thread_create(osal_thread_t* out, osal_thread_fn_t fn, void* arg,
                              const osal_thread_attr_t* attr) {
    OSAL_ASSERT((out != NULL) && (fn != NULL) && (attr != NULL));
    if (!out || !fn || !attr) return OSAL_TASK_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    osThreadAttr_t a;
    a.attr_bits     = 0;
    a.name          = attr->name;
    a.cb_mem        = NULL;
    a.cb_size       = 0;
    a.stack_size    = attr->stack_size;
    a.stack_mem     = NULL;
    a.priority      = OSAL_map_prio(attr->priority);
    a.reserved      = 0;
    a.tz_module     = 0;
    osThreadId_t st = osThreadNew((osThreadFunc_t)fn, arg, &a);
    if (!st) return OSAL_TASK_RET(RET_CLASS_RESOURCE, RET_R_NO_MEM);
    *out = (osal_thread_t)st;
    return RET_OK;
}

/**
 * @brief 获取当前线程 ID
 * @return 当前线程 ID
 */
osal_thread_t OSAL_thread_self(void) {
    return (osal_thread_t)osThreadGetId();
}

/**
 * @brief 设置线程id
 * @param t 线程地址
 * @param flags 要设置的flag
 * @return 是否设置成功
 */
ret_code_t OSAL_thread_flags_set(osal_thread_t t, osal_flags_t flags) {
    OSAL_ASSERT(t != NULL);
    if (!t) return OSAL_TASK_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    const uint32_t r = osThreadFlagsSet((osThreadId_t)t, (uint32_t)flags);
    return ((r & 0x80000000u) == 0u) ? RET_OK : OSAL_TASK_RET(RET_CLASS_FATAL, RET_R_PANIC);
}

/**
 * @brief 指定时间等待flag
 * @param flags 线程 ID
 * @param mode  flag匹配模式
 * @param timeout_ms 指定的等待时间
 * @return 指定时间内是否获取到指定flag
 */
osal_flags_t OSAL_thread_flags_wait(osal_flags_t flags, osal_flags_wait_t mode,
                                    uint32_t timeout_ms) {
    const uint32_t to  = OSAL_timeout_ms_to_kernel_ticks(timeout_ms);
    const uint32_t opt = (mode == OSAL_FLAGS_WAIT_ALL) ? osFlagsWaitAll : osFlagsWaitAny;
    const uint32_t r   = osThreadFlagsWait((uint32_t)flags, opt, to);
    if (r & 0x80000000u) return 0;
    return (osal_flags_t)r;
}
#endif



