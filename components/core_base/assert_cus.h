#ifndef ASSERT_CUS_H
#define ASSERT_CUS_H

#include <stdint.h>

#include "compiler_cus.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===================== 构建策略 ===================== */
/*
 * - DEBUG 构建定义 DEBUG_MODE
 * - RELEASE 构建定义 RELEASE_MODE
 */

/* 是否启用断言系统（建议 RELEASE 也启用 fatal 层） */
#ifndef CORE_ASSERT_ENABLE
#define CORE_ASSERT_ENABLE 1
#endif

/* 是否启用持久化记录（noinit 段） */
#ifndef CORE_ASSERT_ENABLE_RECORD
#define CORE_ASSERT_ENABLE_RECORD 1
#endif

/* 是否启用日志（需要非阻塞日志输出） */
#ifndef CORE_ASSERT_ENABLE_LOG
#define CORE_ASSERT_ENABLE_LOG 1
#endif

/* 默认动作：DEBUG -> HALT+BKPT；RELEASE -> RESET */
typedef enum {
    ASSERT_ACTION_HALT = 0, /* 关中断死循环 */
    ASSERT_ACTION_RESET,    /* NVIC_SystemReset() */
    ASSERT_ACTION_LOG_ONLY, /* 只记录/打印，继续运行（仅限可恢复场景） */
} assert_action_t;
/* 断言等级 */
typedef enum {
    ASSERT_LVL_PARAM = 0,
    ASSERT_LVL_NORMAL,
    ASSERT_LVL_FATAL,
    ASSERT_LVL_RECOVER,
} assert_level_t;

/* 断言全局配置 */
typedef struct {
    assert_action_t action;
    uint8_t enable_log;
    uint8_t enable_record;
} assert_config_t;

/* ===================== 断言失败记录 ===================== */
/* 断言失败记录字段 */
typedef struct {
    uint32_t magic;
    uint32_t count;

    const char *expr;
    const char *file;
    const char *func;
    uint32_t line;

    /* Cortex-M 上下文（通用） */
    uint32_t pc; /* pc指针 */
    uint32_t lr; /* 函数返回地址 */
    uint32_t sp;
    uint32_t psr;

    /* 便于版本追溯 */
    uint32_t build_id;
} assert_record_t;
/*　魔术数字　便于从内存读取判断是否是脏数据　*/
#define ASSERT_RECORD_MAGIC 0xA55E12C3u

/* 运行时配置 修改默认配置 */
void Assert_SetConfig(const assert_config_t *cfg);
/* 获取当前断言配置 */
assert_config_t Assert_GetConfig(void);

/* 读取/清除上次断言记录（用于开机上报） */
const assert_record_t *Assert_GetLastRecord(void);
void Assert_ClearLastRecord(void);

/* 带等级的断言失败入口 显示指定不断言提高获取lr的概率*/
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void Assert_OnFailEx(assert_level_t level, const char *expr, const char *file, const char *func,
                     uint32_t line);

/* ===================== Hooks (weak) ===================== */
/* 非阻塞日志 hook：默认弱定义空实现 */
void Assert_PlatformLog(const char *msg);

/* 平台 reset/halt：默认弱定义实现 */
void Assert_PlatformReset(void);
void Assert_PlatformHalt(void);

/* ===================== 宏定义 ===================== */

#ifndef CORE_LIKELY
#define CORE_LIKELY(x) __builtin_expect(!!(x), 1)
#endif
#ifndef CORE_UNLIKELY
#define CORE_UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif

#if (CORE_ASSERT_ENABLE == 1)
/* 常规断言：DEBUG/RELEASE 都存在 */
#define CORE_ASSERT(expr)                                                                      \
    do {                                                                                       \
        if (CORE_UNLIKELY(!(expr))) {                                                          \
            Assert_OnFailEx(ASSERT_LVL_NORMAL, #expr, __FILE__, __func__, (uint32_t)__LINE__); \
        }                                                                                      \
    } while (0)

/* 参数断言：仅 DEBUG 或显式开启 */
#if defined(DEBUG_MODE) || defined(ASSERT_PARAM_ENABLE)
#define ASSERT_PARAM(expr)                                                                    \
    do {                                                                                      \
        if (CORE_UNLIKELY(!(expr))) {                                                         \
            Assert_OnFailEx(ASSERT_LVL_PARAM, #expr, __FILE__, __func__, (uint32_t)__LINE__); \
        }                                                                                     \
    } while (0)
#else
#define ASSERT_PARAM(expr)  \
    do {                    \
        (void)sizeof(expr); \
    } while (0)
#endif

/* 致命断言：始终在线（用于不可恢复错误） */
#define ASSERT_FATAL(expr)                                                                    \
    do {                                                                                      \
        if (CORE_UNLIKELY(!(expr))) {                                                         \
            Assert_OnFailEx(ASSERT_LVL_FATAL, #expr, __FILE__, __func__, (uint32_t)__LINE__); \
        }                                                                                     \
    } while (0)

#else

#define CORE_ASSERT(expr)   \
    do {                    \
        (void)sizeof(expr); \
    } while (0)
#define ASSERT_PARAM(expr)  \
    do {                    \
        (void)sizeof(expr); \
    } while (0)
#define ASSERT_FATAL(expr)  \
    do {                    \
        (void)sizeof(expr); \
    } while (0)

#endif /* CORE_ASSERT_ENABLE */

/* ===================== Commercial Require/Ensure ===================== */
/* 这类宏用于“可恢复错误”，RELEASE 下不会直接 reset */
/* 断言失败 返回状态码 */
#if (CORE_ASSERT_ENABLE == 1)
#define REQUIRE_RET(expr, retcode)                                                              \
    do {                                                                                        \
        if (CORE_UNLIKELY(!(expr))) {                                                           \
            Assert_OnFailEx(ASSERT_LVL_RECOVER, #expr, __FILE__, __func__, (uint32_t)__LINE__); \
            return (retcode);                                                                   \
        }                                                                                       \
    } while (0)

/* 断言失败就 goto 跳转 */
#define REQUIRE_GOTO(expr, label)                                                               \
    do {                                                                                        \
        if (CORE_UNLIKELY(!(expr))) {                                                           \
            Assert_OnFailEx(ASSERT_LVL_RECOVER, #expr, __FILE__, __func__, (uint32_t)__LINE__); \
            goto label;                                                                         \
        }                                                                                       \
    } while (0)
#else
#define REQUIRE_RET(expr, retcode) \
    do {                           \
        if (!(expr)) return retcode; \
    } while (0)

#define REQUIRE_GOTO(expr, label) \
    do {                          \
        if (!(expr)) goto label; \
    } while (0)
#endif

/* 编译期断言 */
#define STATIC_ASSERT(cond, msg) _Static_assert((cond), msg)

#ifdef __cplusplus
}
#endif
#endif /* ASSERT_CUS_H */
