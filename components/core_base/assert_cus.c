#include "APP_config.h"

/* 全局配置开启宏 */
#if defined(ENABLE_ASSERT_SYSTEM)
#include <stdio.h>
#include <string.h>

#include "assert_cus.h"
#include "compiler_cus.h"

#if defined(__ARMCC_VERSION) || defined(__GNUC__) || defined(__ICCARM__)
#include "cmsis_compiler.h"
#endif

/* ===================== 持续存储 (noinit) ===================== */
/* 放到 noinit 段需要链接脚本支持（NOLOAD），否则复位后记录会丢 */
CORE_SECTION(".noinit.assert")
static volatile assert_record_t g_assert_record;

/* ===================== 默认配置  ===================== */
static assert_config_t g_cfg = {
#if defined(DEBUG_MODE)
    .action = ASSERT_ACTION_HALT,
#elif defined(RELEASE_MODE)
    .action = ASSERT_ACTION_RESET,
#else
    /* 未定义 DEBUG_MODE/RELEASE_MODE 时给一个安全默认值，避免未初始化 */
    .action = ASSERT_ACTION_HALT,
#endif
    .enable_log    = (CORE_ASSERT_ENABLE_LOG ? 1u : 0u),
    .enable_record = (CORE_ASSERT_ENABLE_RECORD ? 1u : 0u),
};

/* ===================== 内部工具 ===================== */

static inline uint32_t read_sp(void) {
#if defined(__GNUC__)
    uint32_t sp;
    __asm volatile("mov %0, sp" : "=r"(sp));
    return sp;
#else
    /* 其他编译器待适配 */
    return 0u;
#endif
}

/* 尽力捕获 PC/LR/PSR：
 * - 通过 __builtin_return_address(0) 获取“调用点返回地址”作为近似 PC
 * - LR 通过 __builtin_return_address(0/1) 这里以 0 为主
 * - xPSR 读取 IPSR/APSR 需要内联汇编或 CMSIS，做尽力而为
 */
static inline uint32_t approx_pc(void) {
#if defined(__GNUC__) || defined(__clang__)
    uint32_t pc;
    __asm volatile("adr %0, ." : "=r"(pc));
    return pc;
#else
    return 0u;
#endif
}

static inline uint32_t approx_lr(void) {
#if defined(__GNUC__) || defined(__clang__)
    uint32_t lr;
    __asm volatile("mov %0, lr" : "=r"(lr));
    return lr;
#else
    return 0u;
#endif
}

static inline uint32_t read_psr(void) {
#if defined(__GNUC__)
    uint32_t psr;
    __asm volatile("mrs %0, xPSR" : "=r"(psr));
    return psr;
#else
    return 0u;
#endif
}

/**
 * @brief 获取断言失败的相关信息
 * @param expr 断言失败的原因
 * @param file 文件名
 * @param func 函数名
 * @param line 具体哪一行
 */
static void record_assert(const char* expr, const char* file, const char* func, uint32_t line) {
    if (!g_cfg.enable_record) return;

    if (g_assert_record.magic != ASSERT_RECORD_MAGIC) {
        g_assert_record.magic = ASSERT_RECORD_MAGIC;
        g_assert_record.count = 0u;
    }
    g_assert_record.count++;

    /* 获取断言失败的场景信息 文本层面*/
    g_assert_record.expr = expr;
    g_assert_record.file = file;
    g_assert_record.func = func;
    g_assert_record.line = line;

    /* build_id：建议由编译系统注入（例如 git hash 截断），此处给默认 0 */
#ifndef CORE_BUILD_ID
#define CORE_BUILD_ID 0u
#endif
    g_assert_record.build_id = (uint32_t)CORE_BUILD_ID;
}

/* ===================== 公共API ===================== */
/**
 * @brief 配置断言全局参数
 * @param cfg 需要传入的 配置句柄
 */
void Assert_SetConfig(const assert_config_t* cfg) {
    if (cfg) {
        g_cfg = *cfg;
    }
}
/**
 * @brief 获取全局配置
 * @return 返回断言全局配置的副本
 */
assert_config_t Assert_GetConfig(void) {
    return g_cfg;
}
/**
 * @brief 获取上次断言失败的信息
 * @return 返回断言失败的信息结构体
 */
const assert_record_t* Assert_GetLastRecord(void) {
    if (g_assert_record.magic == ASSERT_RECORD_MAGIC) {
        return (const assert_record_t*)&g_assert_record;
    }
    return NULL;
}
/**
 * @brief 清理上次的断言失败信息记录
 */
void Assert_ClearLastRecord(void) {
    /* 不用 memset(0) 也可，只清 magic 最快 */
    g_assert_record.magic = 0u;
}

/* ===================== 弱实现钩子 ===================== */
/**
 * @brief 通过传入消息字符串异步输出
 * @param msg 消息字符串
 */
__WEAK void Assert_PlatformLog(const char* msg) {
    (void)msg;
    /* 空 */
}
/**
 * @brief 系统软件复位
 * @note 必须上层覆盖该函数  NVIC_SystemReset 宏否则退化到 Assert_PlatformHalt()
 */
__WEAK void Assert_PlatformReset(void) {
    Assert_PlatformHalt();
}
/**
 * @brief 根据编译器平台和编译模式 执行不同的断言错误程序流程
 */
__WEAK void Assert_PlatformHalt(void) {
#if defined(__arm__) || defined(__ARM_ARCH)
    __disable_irq();
#endif

#if defined(DEBUG_MODE)
    /* 尽可能进调试器 */
#if defined(__GNUC__)
    __asm volatile("bkpt 0");
#endif
#endif

    while (1) {
        /* 死循环等待 */
    }
}

/* ===================== 核心处理  ===================== */

/* 内部：统一输出一条日志（可复用） */
static void assert_log_common(const char* expr, const char* file, const char* func, uint32_t line) {
    if (g_cfg.enable_log) {
        char buf[160];
        /* 待更改为更轻的格式化 */
        (void)snprintf(buf, sizeof(buf),
                       "ASSERT FAIL: %s (%s:%lu) %s pc=0x%08lx lr=0x%08lx sp=0x%08lx psr=0x%08lx "
                       "cnt=%lu build=0x%08lx",
                       expr, file, (unsigned long)line, func, (unsigned long)g_assert_record.pc,
                       (unsigned long)g_assert_record.lr, (unsigned long)g_assert_record.sp,
                       (unsigned long)g_assert_record.psr, (unsigned long)g_assert_record.count,
                       (unsigned long)g_assert_record.build_id);
        Assert_PlatformLog(buf);
    }
}

/**
 * 新增：带等级的断言入口
 * - RECOVER：只记录/打印然后返回（用于 REQUIRE_RET / REQUIRE_GOTO 语义）
 * - FATAL：Release 强制 reset/halt（不允许 LOG_ONLY 带病运行）
 * - NORMAL：按 g_cfg.action 执行
 * - PARAM：通常仅 Debug 生效；若走到这里，按 NORMAL 处理
 */
void Assert_OnFailEx(assert_level_t level, const char* expr, const char* file, const char* func,
                     uint32_t line) {
    /* 获取断言失败的上下文 寄存器层面 */
    g_assert_record.sp  = read_sp();
    g_assert_record.pc  = (uint32_t)__builtin_return_address(0);
    g_assert_record.lr  = approx_lr();
    g_assert_record.psr = read_psr();

    /* 文件层面记录 错误环境信息 */
    record_assert(expr, file, func, line);

    /* 异步输出日志 */
    assert_log_common(expr, file, func, line);

    /* RECOVER：仅允许用于“明确可恢复”的断言 */
    if (level == ASSERT_LVL_RECOVER) {
        return;
    }

    /* FATAL：致命错误，不允许继续运行 */
    if (level == ASSERT_LVL_FATAL) {
#if defined(RELEASE_MODE)
        /* Release：强制复位或 fail-stop，避免带病运行 */
        Assert_PlatformReset();
        /* 复位失败 */
        Assert_PlatformHalt();
        return;
#elif defined(DEBUG_MODE)
        /* Debug：直接停机进调试器 */
        Assert_PlatformHalt();
        return;
#endif
    }

    /* 其余等级（NORMAL / PARAM）：按全局配置执行处理 */
    switch (g_cfg.action) {
        case ASSERT_ACTION_LOG_ONLY:
            /* 仅允许用于“明确可恢复”的断言（业务可控），否则会引入隐患 */
            return;

        case ASSERT_ACTION_RESET:
            Assert_PlatformReset();
            /* 理论上不返回 */
            Assert_PlatformHalt();
            return;

            /* DEBUG 编译下处理*/
        case ASSERT_ACTION_HALT:
        default:
            Assert_PlatformHalt();
            return;
    }
}

#endif /* ENABLE_ASSERT_SYSTEM */
