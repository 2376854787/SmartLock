#include "blackbox_record.h"

#include <stddef.h>

#include "blackbox_port_reset_reason.h"
#include "compiler_cus.h"

#ifndef CORE_BUILD_ID
#define CORE_BUILD_ID 0u
#endif

CORE_SECTION(".noinit.blackbox")
static volatile blackbox_record_t g_bb;

/**
 * @brief 返回全局变量的地址
 * @return 返回全局变量的地址
 */
const blackbox_record_t* BB_Get(void) {
    if (g_bb.magic == BLACK_BOX_MAGIC && g_bb.version == BLACK_BOX_VERSION) {
        return (const blackbox_record_t*)&g_bb;
    }
    return NULL;

}
/**
 * @brief 清理黑盒子全局变量
 */
void BB_Clear(void) {
    g_bb.magic = 0;
}

/**
 * @brief 更新 Reset原因以及 清除标志位
 */
void BB_OnBootUpdateResetReason(void) {
    /* 诊断：打印进入时的原始值（在 UART 初始化后才能看到，可用调试器查看） */
    volatile uint32_t raw_magic   = g_bb.magic;
    volatile uint32_t raw_version = g_bb.version;
    volatile uint32_t raw_count   = g_bb.boot_count;
    (void)raw_magic; /* 防止优化掉，调试器可查看 */
    (void)raw_version;
    (void)raw_count;

    if (g_bb.version != BLACK_BOX_VERSION || g_bb.magic != BLACK_BOX_MAGIC) {
        g_bb.magic          = BLACK_BOX_MAGIC;
        g_bb.version        = BLACK_BOX_VERSION;
        g_bb.boot_count     = 0u;
        g_bb.max_crit_us    = 0u;
        g_bb.min_stack_free = UINT32_MAX;
    }
    g_bb.boot_count++;
    g_bb.build_id     = (uint32_t)CORE_BUILD_ID;
    g_bb.reset_reason = (uint32_t)BB_Port_ReadResetReasonAndClearFlags();
    g_bb.crash_type   = BB_CRASH_NONE;
}

/**
 * @brief 在断言失败时调用写入黑盒子
 * @param pc pc寄存器值
 * @param lr lr寄存器值
 * @param sp sp寄存器值
 * @param psr psr寄存器值
 */
void BB_RecordAssert(uint32_t pc, uint32_t lr, uint32_t sp, uint32_t psr) {
    g_bb.crash_type = BB_CRASH_ASSERT;
    g_bb.pc         = pc;
    g_bb.lr         = lr;
    g_bb.sp         = sp;
    g_bb.psr        = psr;
}

/**
 *
 * @param type 崩溃类型
 * @param pc pc寄存器值
 * @param lr lr寄存器值
 * @param sp sp寄存器值
 * @param psr psr寄存器值
 * @param cfsr 配置错误状态寄存器值
 * @param hfsr 硬错误状态寄存器值
 * @param dfsr 调式错误状态寄存器值
 * @param mmfar 内存管理错误状态寄存器值
 * @param bfsr 总线错误状态寄存器值
 * @param afsr 辅助故障抓鬼太寄存器值
 */
void BB_RecordFault(bb_crash_type_t type, uint32_t pc, uint32_t lr, uint32_t sp, uint32_t psr,
                    uint32_t cfsr, uint32_t hfsr, uint32_t dfsr, uint32_t mmfar, uint32_t bfsr,
                    uint32_t afsr) {
    g_bb.crash_type   = type;
    g_bb.pc           = pc;
    g_bb.lr           = lr;
    g_bb.sp           = sp;
    g_bb.psr          = psr;
    g_bb.cfsr         = cfsr;
    g_bb.hfsr         = hfsr;
    g_bb.dfsr         = dfsr;
    g_bb.mmfar        = mmfar;
    g_bb.bfar         = bfsr;
    g_bb.afsr         = afsr;
    g_bb.reset_reason = (uint32_t)BB_CRASH_HARDFAULT;
}
/**
 * @brief 保存当前运行的状态机的句柄地址和状态
 * @param fsm_ptr 状态机句柄的地址
 * @param state_ptr 状态机当前状态
 */
void BB_RecordFsm(uint32_t fsm_ptr, uint32_t state_ptr) {
    g_bb.fsm_ptr   = fsm_ptr;
    g_bb.state_ptr = state_ptr;
}
/**
 * @brief 记录最大关中断时间
 * @param us 关中断的时间
 */
void BB_UpdateMaxCriUs(uint32_t us) {
    if (us > g_bb.max_crit_us) {
        g_bb.max_crit_us = us;
    }
}
/**
 * @brief 记录栈当前最低水位线的字节数
 * @param bytes 剩余字节数
 */
void BB_UpdateMinStackFree(uint32_t bytes) {
    if (bytes < g_bb.min_stack_free) g_bb.min_stack_free = bytes;
}