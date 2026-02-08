#ifndef SMARTLOCK_AUTO_INIT_H
#define SMARTLOCK_AUTO_INIT_H
#include <stdint.h>

#include "compiler_cus.h"

/* 容错定义 */
#ifndef CORE_USED
#define CORE_USED __attribute__((used))
#endif
#ifndef CORE_SECTION
#define CORE_SECTION(x) __attribute__((section(x)))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*auto_init_fn_t)(void);

/* ================= 1. 枚举定义 (保持原样) ================= */
typedef enum {
    AUTO_INIT_EARLY = 0,
    AUTO_INIT_INIT  = 1,
    AUTO_INIT_LATE  = 2,
    AUTO_INIT_LEVEL_MAX
} auto_init_level_t;

typedef enum {
    AUTO_BUCKET_0 = 0,
    AUTO_BUCKET_1,
    AUTO_BUCKET_2,
    AUTO_BUCKET_3,
    AUTO_BUCKET_4,
    AUTO_BUCKET_5,
    AUTO_BUCKET_6,
    AUTO_BUCKET_7,
    AUTO_BUCKET_MAX = 8
} auto_init_bucket_t;

typedef struct {
    auto_init_fn_t fn;
    uint16_t prio;
    uint16_t _rsv;
    const char *name;
} auto_init_item_t;

/* ================= 2. 基础字符串定义 ================= */
#define SEC_STR_EARLY_0 ".auto_init.early.b0"
#define SEC_STR_EARLY_1 ".auto_init.early.b1"
#define SEC_STR_EARLY_2 ".auto_init.early.b2"
#define SEC_STR_EARLY_3 ".auto_init.early.b3"
#define SEC_STR_EARLY_4 ".auto_init.early.b4"
#define SEC_STR_EARLY_5 ".auto_init.early.b5"
#define SEC_STR_EARLY_6 ".auto_init.early.b6"
#define SEC_STR_EARLY_7 ".auto_init.early.b7"

#define SEC_STR_INIT_0 ".auto_init.init.b0"
#define SEC_STR_INIT_1 ".auto_init.init.b1"
#define SEC_STR_INIT_2 ".auto_init.init.b2"
#define SEC_STR_INIT_3 ".auto_init.init.b3"
#define SEC_STR_INIT_4 ".auto_init.init.b4"
#define SEC_STR_INIT_5 ".auto_init.init.b5"
#define SEC_STR_INIT_6 ".auto_init.init.b6"
#define SEC_STR_INIT_7 ".auto_init.init.b7"

#define SEC_STR_LATE_0 ".auto_init.late.b0"
#define SEC_STR_LATE_1 ".auto_init.late.b1"
#define SEC_STR_LATE_2 ".auto_init.late.b2"
#define SEC_STR_LATE_3 ".auto_init.late.b3"
#define SEC_STR_LATE_4 ".auto_init.late.b4"
#define SEC_STR_LATE_5 ".auto_init.late.b5"
#define SEC_STR_LATE_6 ".auto_init.late.b6"
#define SEC_STR_LATE_7 ".auto_init.late.b7"

/* ================= 3. 映射表  ================= */
#define MAP_LVL_AUTO_INIT_EARLY EARLY
#define MAP_LVL_AUTO_INIT_INIT  INIT
#define MAP_LVL_AUTO_INIT_LATE  LATE

#define MAP_BKT_AUTO_BUCKET_0 0
#define MAP_BKT_AUTO_BUCKET_1 1
#define MAP_BKT_AUTO_BUCKET_2 2
#define MAP_BKT_AUTO_BUCKET_3 3
#define MAP_BKT_AUTO_BUCKET_4 4
#define MAP_BKT_AUTO_BUCKET_5 5
#define MAP_BKT_AUTO_BUCKET_6 6
#define MAP_BKT_AUTO_BUCKET_7 7
/* 兼容直接写数字 */
#define MAP_BKT_0 0
#define MAP_BKT_1 1
#define MAP_BKT_2 2
#define MAP_BKT_3 3
#define MAP_BKT_4 4
#define MAP_BKT_5 5
#define MAP_BKT_6 6
#define MAP_BKT_7 7

/* ================= 4. 核心宏逻辑  ================= */

/* 第一层：基础拼接工具 */
#define AUTO_CONCAT_FINAL(a, b, c) a##b##_##c

/* 第二层：中间层（关键！）
 * 在这一层，不使用 ##，迫使参数 l 和 b 先展开
 * 例如：l=MAP_LVL_AUTO_INIT_EARLY 会先展开成 EARLY
 */
#define AUTO_CONCAT_EXPAND(l, b) AUTO_CONCAT_FINAL(SEC_STR_, l, b)

/* 第三层：入口
 * 先拼接前缀 MAP_LVL_ 和参数，得到 MAP_LVL_AUTO_INIT_EARLY
 * 然后传给第二层去展开
 */
#define GET_SEC_NAME(level, bucket) AUTO_CONCAT_EXPAND(MAP_LVL_##level, MAP_BKT_##bucket)

#define AUTO_INIT_CONCAT_BASE(a, b) a##b

/* 2. 顶层展开宏 (调用这个) */
/* 这里的中间层会让传入的 __LINE__ 先展开成 189，再传给 BASE 进行拼接 */
#define AUTO_INIT_CONCAT(a, b) AUTO_INIT_CONCAT_BASE(a, b)
/* ================= 5. 注册宏 ================= */


#define AUTO_INIT_REG(level_, bucket_, prio_, fn_)                                 \
    _Static_assert((prio_) >= 0 && (prio_) <= 64, "AutoInit: Priority 0-64 only"); \
    static const auto_init_item_t AUTO_INIT_CONCAT(__auto_init_item_, __LINE__)    \
    CORE_USED                                                                      \
    CORE_SECTION(GET_SEC_NAME(level_, bucket_)) = {(fn_), (uint16_t)(prio_), 0u, #fn_}

void auto_init_run_all(void);

#ifdef __cplusplus
}
#endif
#endif