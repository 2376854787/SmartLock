#include "fault_capture_cm.h"

#include "assert_cus.h"
#include "fault_capture.h"

/* 确保能访问 SCB：一般 stm32xxxx.h 已经包含 core_cm*.h */
#include "stm32_hal.h" /* 或直接 include 对应芯片头 stm32f4xx.h/stm32h7xx.h/... */

/**
 * Cortex-M 异常栈帧结构
 * 基本帧：8 个 uint32_t (r0, r1, r2, r3, r12, lr, pc, psr) = 32 bytes
 * FPU 扩展帧：基本帧 + 18 个 uint32_t (s0-s15, fpscr, reserved) = 104 bytes
 *
 * 判断方法：检查 EXC_RETURN (LR) 的 bit 4
 *   - bit4 = 1: 基本帧 (无 FPU 上下文)
 *   - bit4 = 0: 扩展帧 (有 FPU 上下文)
 */

/* 基本栈帧偏移 (以 uint32_t 为单位) */
#define STACK_FRAME_R0  0
#define STACK_FRAME_R1  1
#define STACK_FRAME_R2  2
#define STACK_FRAME_R3  3
#define STACK_FRAME_R12 4
#define STACK_FRAME_LR  5
#define STACK_FRAME_PC  6
#define STACK_FRAME_PSR 7

void FaultCapture_FromStack_cm(uint32_t* sp, bb_crash_type_t type) {
    ASSERT_FATAL(sp != NULL);
    if (sp == NULL) {
        while (1) {
        }
    }
    /* 直接从栈指针偏移读取，避免结构体对齐问题 */
    fault_ctx_t ctx;
    ctx.pc  = sp[STACK_FRAME_PC];
    ctx.lr  = sp[STACK_FRAME_LR];
    ctx.sp  = (uint32_t)(uintptr_t)sp;
    ctx.psr = sp[STACK_FRAME_PSR];

    /* SCB 错误寄存器 */
#if (__CORTEX_M >= 3)
    ctx.cfsr  = SCB->CFSR;
    ctx.hfsr  = SCB->HFSR;
    ctx.dfsr  = SCB->DFSR;
    ctx.mmfar = SCB->MMFAR;
    ctx.bfar  = SCB->BFAR;
    ctx.afsr  = SCB->AFSR;
#elif (__CORTEX_M < 3)
    ctx.cfsr  = 0;
    ctx.hfsr  = 0;
    ctx.dfsr  = 0;
    ctx.mmfar = 0;
    ctx.bfar  = 0;
    ctx.afsr  = 0;
#endif

    /* 平台无关提交 */
    FaultRecord_Commit(type, &ctx);

    /* 策略：fail-stop 或者 NVIC_SystemReset() */
    while (1) { /* trap */
    }
}
