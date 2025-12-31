//
// Created by yan on 2025/12/20.
//

#ifndef SMARTLOCK_ASSERT_CUS_H
#define SMARTLOCK_ASSERT_CUS_H
#include <stdint.h>
#include "compiler_cus.h"

#ifdef __cplusplus
extern "C" {



#endif
/* 断言后的策略 */
typedef enum {
    ASSERT_ACTION_HALT = 0, // 关中断死循环（默认最安全）
    ASSERT_ACTION_RESET, // NVIC_SystemReset()
    ASSERT_ACTION_LOG_ONLY, // 只记录/打印，继续运行（不推荐用于严重错误）
} assert_action_t;

/* 断言配置 */
typedef struct {
    assert_action_t action;

    /* 是否尝试打印（异步 log 系统） */
    uint8_t enable_log;

    /* 是否记录到 noinit 区（便于复位后读取） */
    uint8_t enable_record;
} assert_config_t;

/* 运行时配置（可选） */
void Assert_SetConfig(const assert_config_t *cfg);

assert_config_t Assert_GetConfig(void);

/* 断言失败处理入口（不要直接调用，使用宏） */
void Assert_OnFail(const char *expr,
                   const char *file,
                   int line);

/* 可选：更“致命”的 fault assert（语义上表示系统不该继续运行） */
#define CORE_FAULT_ASSERT(expr) do { \
if (CORE_UNLIKELY(!(expr))) { Assert_OnFail(#expr, __FILE__, __LINE__); } \
} while (0)

/* 常规 assert */
#define CORE_ASSERT(expr) CORE_FAULT_ASSERT(expr)

#ifdef __cplusplus
}
#endif
#endif //SMARTLOCK_ASSERT_CUS_H
