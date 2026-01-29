#include "fault_capture_cm.h"

#include "fault_capture.h"

/* 确保能访问 SCB：一般 stm32xxxx.h 已经包含 core_cm*.h */
#include "stm32_hal.h" /* 或直接 include 对应芯片头 stm32f4xx.h/stm32h7xx.h/... */

typedef struct {
    uint32_t r0, r1, r2, r3, r12, lr, pc, psr;
} cm_stacked_frame_t;

void FaultCapture_FromStack_cm(uint32_t* sp, bb_crash_type_t type) {
    const cm_stacked_frame_t* f = (const cm_stacked_frame_t*)sp;

    fault_ctx_t ctx;
    ctx.pc    = f->pc;
    ctx.lr    = f->lr;
    ctx.sp    = (uint32_t)(uintptr_t)sp;
    ctx.psr   = f->psr;

    /* SCB fault registers (Cortex-M) */
    ctx.cfsr  = SCB->CFSR;
    ctx.hfsr  = SCB->HFSR;
    ctx.dfsr  = SCB->DFSR;
    ctx.mmfar = SCB->MMFAR;
    ctx.bfar  = SCB->BFAR;
    ctx.afsr  = SCB->AFSR;

    /* 平台无关提交 */
    FaultRecord_Commit(type, &ctx);

    /* 策略：fail-stop 或者 NVIC_SystemReset() */
    while (1) { /* trap */
    }
}
