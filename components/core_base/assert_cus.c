
#include "APP_config.h"
/* 全局配置开启宏 */
#ifdef ENABLE_ASSERT_SYSTEM
#include "assert_cus.h"
#include <string.h>
#include "compiler_cus.h"
/*  assert 里打日志，把 log.h include 进来
 * assert 不能依赖“可能阻塞”的输出路径；只在任务态且 log 异步就绪时打印
 */


#ifdef ASSERT_USE_LOG
#include "log.h"
#endif
#define STM32F4
#if defined(STM32F1) || defined(STM32F4) || defined(STM32F0) || defined(STM32F7) || defined(STM32H7)
#include "stm32f4xx.h"
#endif

/* ========= 可选：断言记录（noinit 段） ========= */
typedef struct {
    uint32_t magic;
    uint32_t count;
    const char *expr;
    const char *file;
    int line;
} assert_record_t;

#define ASSERT_RECORD_MAGIC 0xA55E12C3u

/* 放到 noinit 段需要链接脚本支持 否则记录不保留。 */
CORE_SECTION(".noinit.assert")
static volatile assert_record_t g_assert_record;

/* 断言运行默认配置 */
static assert_config_t g_cfg = {
    .action = ASSERT_ACTION_HALT,
    .enable_log = 1,
    .enable_record = 1,
};

/**
 * @brief 配置断言运行配置
 * @param cfg 配置句柄
 */
void Assert_SetConfig(const assert_config_t *cfg) {
    if (cfg) g_cfg = *cfg;
}

/**
 *
 * @return 返回当前断言运行配置
 */
assert_config_t Assert_GetConfig(void) {
    return g_cfg;
}

/**
 *
 * @param expr 字符串
 * @param file 文件名
 * @param line 代码行数
 */
static void assert_record(const char *expr, const char *file, int line) {
    if (!g_cfg.enable_record) return;

    if (g_assert_record.magic != ASSERT_RECORD_MAGIC) {
        g_assert_record.magic = ASSERT_RECORD_MAGIC;
        g_assert_record.count = 0;
    }
    g_assert_record.count++;
    g_assert_record.expr = expr;
    g_assert_record.file = file;
    g_assert_record.line = line;
}

/* 你可以在项目层提供一个同名非 weak 的实现，覆盖该行为 */
__WEAK void Assert_PlatformReset(void) {


#if defined(NVIC_SystemReset)
NVIC_SystemReset();
#else
/* 降级处理：死循环停机 */
while (1) { ; }
#endif
}

__WEAK void Assert_PlatformHalt(void) {


/* 关中断，避免系统继续跑出二次破坏 */
#if defined(__arm__) || defined(__ARM_ARCH)
__disable_irq();
#endif
while (1) { ; }
}

void Assert_OnFail(const char *expr, const char *file, int line) {
    assert_record(expr, file, line);

#ifdef ASSERT_USE_LOG
if (g_cfg.enable_log) {
        /* 异步输出 */
        LOG_E("ASSERT", "FAIL: %s (%s:%d)", expr, file, line);
    }
#endif

switch (g_cfg.action) {
        case ASSERT_ACTION_LOG_ONLY:
            return;

        case ASSERT_ACTION_RESET:
            Assert_PlatformReset();
            return;

        case ASSERT_ACTION_HALT:
        default:
            Assert_PlatformHalt();
            return;
    }
}


#endif
