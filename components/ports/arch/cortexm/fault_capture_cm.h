#ifndef FAULT_CAPTURE_CM_H
#define FAULT_CAPTURE_CM_H

#include <stdint.h>

#include "blackbox_record.h"
#ifdef __cplusplus
extern "C" {
#endif

/* 从异常栈帧抓取上下文并提交到 blackbox，然后 fail-stop/reset */
void FaultCapture_FromStack_cm(uint32_t* sp, bb_crash_type_t type);

/* 提供给 stm32f4xx_it.c 中 Handler 调用的宏
 * 使用方法：在 HardFault_Handler 等函数开头调用 FAULT_CAPTURE_HANDLER(BB_CRASH_xxx)
 * 注意：此宏会进入死循环，不会返回
 */
#if defined(__GNUC__) || defined(__clang__)
#define FAULT_CAPTURE_HANDLER(type)             \
    do {                                        \
        register uint32_t* _sp;                 \
        __asm volatile(                         \
            "tst lr, #4        \n"              \
            "ite eq            \n"              \
            "mrseq %0, msp     \n"              \
            "mrsne %0, psp     \n"              \
            : "=r"(_sp));                       \
        FaultCapture_FromStack_cm(_sp, (type)); \
    } while (0)
#else
#define FAULT_CAPTURE_HANDLER(type)                      \
    do {                                                 \
        FaultCapture_FromStack_cm((uint32_t*)0, (type)); \
    } while (0)
#endif

#ifdef __cplusplus
}
#endif

#endif
