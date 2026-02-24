#include "blackbox_record.h"

#include "compiler_cus.h"

#ifndef CORE_BUILD_ID
#define CORE_BUILD_ID 0u
#endif
#include <stdio.h>

#include "assert_cus.h"
/* 配置宏：定义此宏使用备份SRAM (0x40024000)，否则使用 .noinit 段 (SRAM1/2) */
#define CONFIG_BLACKBOX_USE_BKPSRAM 1

#ifndef CORE_BUILD_ID
#define CORE_BUILD_ID 0u
#endif

/**
 * @brief 默认实现 致命断言
 * @return 备份域是否时钟已经使能
 */
CORE_WEAK bool BB_Clock_is_ready(void) {
    ASSERT_FATAL(0);
    return false;
}
/**
 * @brief  默认实现 致命断言
 * @return 复位原因
 */
CORE_WEAK bb_reset_reason_t BB_Port_ReadResetReasonAndClearFlags(void) {
    ASSERT_FATAL(0);
    return BB_RESET_ASSERT;
}
#if defined(CONFIG_BLACKBOX_USE_BKPSRAM) && (CONFIG_BLACKBOX_USE_BKPSRAM == 1)
/* =================================================================================
 *  Backup SRAM
 * 地址：0x4002 4000 (4KB)
 * ================================================================================= */
#define BKPSRAM_BASE 0x40024000UL

/* 为了方便调试器查看，定义一个具体的指针变量，在 Watch 中添加 *g_bb_debug_ptr 即可查看内容 */
CORE_USED volatile blackbox_record_t* const g_bb_debug_ptr =
    (volatile blackbox_record_t*)BKPSRAM_BASE;

/* 代码中使用宏展开访问，兼顾性能 */
#define g_bb (*(volatile blackbox_record_t*)BKPSRAM_BASE)
CORE_WEAK void BB_EnableAccess(void) {
    /* 默认实现 没有port层实现直接致命断言*/
    ASSERT_FATAL(0);
}
#else
/* =================================================================================
 *  .noinit 段
 * 特性：仅在由于 NVIC_SystemReset() 等非断电/非调试器复位时保留
 * ================================================================================= */
CORE_SECTION(".noinit.blackbox")
static volatile blackbox_record_t g_bb_instance;

/* 为了统一调试体验，也定义一个调试指针 */
CORE_USED volatile blackbox_record_t* const g_bb_debug_ptr = &g_bb_instance;

#define g_bb g_bb_instance

static void BB_EnableAccess(void) {
    /* 普通 SRAM 无需特殊时钟/电源使能 */
}
#endif

/**
 * @brief 返回全局变量的地址
 * @return 返回全局变量的地址
 */
const blackbox_record_t* BB_Get(void) {
#if defined(CONFIG_BLACKBOX_USE_BKPSRAM) && (CONFIG_BLACKBOX_USE_BKPSRAM == 1)
    if (!BB_Clock_is_ready()) {
        BB_EnableAccess();
    }
#endif

    if (g_bb.magic == BLACK_BOX_MAGIC && g_bb.version == BLACK_BOX_VERSION) {
        return (const blackbox_record_t*)&g_bb;
    }
    return NULL;
}

/**
 * @brief 清理黑盒子全局变量
 */
void BB_Clear(void) {
    BB_EnableAccess();
    g_bb.magic = 0;
}

/**
 * @brief 仅清除崩溃信息（crash_type 和相关寄存器）
 * @note  在 App 成功读取并上报崩溃日志后调用
 */
void BB_ClearCrashInfo(void) {
    BB_EnableAccess();
    g_bb.crash_type  = BB_CRASH_NONE;
    g_bb.pc          = 0;
    g_bb.lr          = 0;
    g_bb.sp          = 0;
    g_bb.psr         = 0;
    g_bb.cfsr        = 0;
    g_bb.hfsr        = 0;
    g_bb.dfsr        = 0;
    g_bb.mmfar       = 0;
    g_bb.bfar        = 0;
    g_bb.afsr        = 0;
    g_bb.max_crit_us = 0;
}
/**
 * @brief 清除看门狗异常黑盒信息
 */
void BB_ClearWdgInfo(void) {
    BB_EnableAccess();
    g_bb.wdg.valid    = 0u;
    g_bb.wdg.task_id  = 0u;
    g_bb.wdg.reason   = 0u;
    g_bb.wdg.seq      = 0u;
    g_bb.wdg.nonce    = 0u;
    g_bb.wdg.expected = 0u;
    g_bb.wdg.got      = 0u;
    g_bb.wdg.tick_ms  = 0u;
}
/**
 * @brief 更新 Reset原因以及清除标志位
 * @note  App 全权负责 blackbox 数据读写
 */
void BB_OnBootUpdateResetReason(void) {
    BB_EnableAccess();

    if (g_bb.version != BLACK_BOX_VERSION || g_bb.magic != BLACK_BOX_MAGIC) {
        /* 首次启动或数据损坏：完全初始化 */
        g_bb.magic          = BLACK_BOX_MAGIC;
        g_bb.version        = BLACK_BOX_VERSION;
        g_bb.boot_count     = 0u;
        g_bb.max_crit_us    = 0u;
        g_bb.min_stack_free = UINT32_MAX;
        g_bb.crash_type     = BB_CRASH_NONE;
        g_bb.wdg.valid      = 0u;
        g_bb.wdg.task_id    = 0u;
        g_bb.wdg.reason     = 0u;
        g_bb.wdg.seq        = 0u;
        g_bb.wdg.nonce      = 0u;
        g_bb.wdg.expected   = 0u;
        g_bb.wdg.got        = 0u;
        g_bb.wdg.tick_ms    = 0u;
    }
    /* 始终读取 reset_reason 并清除 CSR flags */
    g_bb.reset_reason = (uint32_t)BB_Port_ReadResetReasonAndClearFlags();
    g_bb.boot_count++;
    g_bb.build_id = (uint32_t)CORE_BUILD_ID;
    /* 注意：不清除 crash_type，由上层决定何时清除 */
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
 * @brief 记录错误的环境信息以及原因
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
    g_bb.reset_reason = (uint32_t)type;
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

/**
 * @brief 记录看门狗失败快照（复位前调用）
 */
void BB_RecordWdgFail(const bb_wdg_fail_record_t* record) {
    ASSERT_PARAM(record != NULL);
    if (record == NULL) return;
    g_bb.wdg.valid    = record->valid;
    g_bb.wdg.task_id  = record->task_id;
    g_bb.wdg.reason   = record->reason;
    g_bb.wdg.seq      = record->seq;
    g_bb.wdg.nonce    = record->nonce;
    g_bb.wdg.expected = record->expected;
    g_bb.wdg.got      = record->got;
    g_bb.wdg.tick_ms  = record->tick_ms;
}

/**
 * @brief 清除看门狗失败快照
 */
void BB_ClearWdgFail(void) {
    g_bb.wdg.valid    = 0u;
    g_bb.wdg.task_id  = 0u;
    g_bb.wdg.reason   = 0u;
    g_bb.wdg.seq      = 0u;
    g_bb.wdg.nonce    = 0u;
    g_bb.wdg.expected = 0u;
    g_bb.wdg.got      = 0u;
    g_bb.wdg.tick_ms  = 0u;
}

/**
 * @brief 上电打印可能会有的错误信息
 */
void BB_Info_Printf() {
    BB_EnableAccess(); /* 必须先使能 Backup SRAM 访问 */
    printf(
        "复位原因: 0x%08X,  崩溃原因: %d\r\n"
        "PC: 0x%08X, LR: 0x%08X\r\n"
        "SP: 0x%08X, PSR: 0x%08X\r\n"
        "WDG有效: %lu, id: %lu, reason: %lu\r\n"
        "WDG seq: 0x%08lX, nonce: 0x%08lX\r\n"
        "WDG expected: 0x%08lX, got: 0x%08lX, tick: %lu\r\n",
        (unsigned int)g_bb.reset_reason, (unsigned int)g_bb.crash_type, (unsigned int)g_bb.pc,
        (unsigned int)g_bb.lr, (unsigned int)g_bb.sp, (unsigned int)g_bb.psr,
        (unsigned long)g_bb.wdg.valid, (unsigned long)g_bb.wdg.task_id,
        (unsigned long)g_bb.wdg.reason, (unsigned long)g_bb.wdg.seq, (unsigned long)g_bb.wdg.nonce,
        (unsigned long)g_bb.wdg.expected, (unsigned long)g_bb.wdg.got,
        (unsigned long)g_bb.wdg.tick_ms);
}
