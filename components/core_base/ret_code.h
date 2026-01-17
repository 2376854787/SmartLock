#ifndef SMARTLOCK_RET_CODE_H
#define SMARTLOCK_RET_CODE_H

#include <stdbool.h>
#include <stdint.h>

#include "compiler_cus.h"
#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * ret_code_t：32 位分段错误码（Packed Code）
 *
 * 位布局：
 *   [31:24] 模块 ID（Module ID）
 *   [23:0 ] 错误类型（Errno）
 *
 * 约定：
 *   - 0 表示成功（RET_OK）
 *   - 非 0 表示失败（ret_is_err(rc) == true）
 * =============================================================================
 */
typedef uint32_t ret_code_t;

/* -----------------------------------------------------------------------------
 * 模块 ID（高 8 位）
 * 建议：每个模块返回自己模块 ID 的错误码，便于定位错误来源。
 * -----------------------------------------------------------------------------
 */
typedef enum {
    RET_MOD_NONE = 0x00u, /* 保留 */
    RET_MOD_CORE = 0x01u, /* core_base / 通用核心 */
    RET_MOD_OSAL = 0x02u, /* OS 抽象层 */
    RET_MOD_HAL  = 0x03u, /* HAL 抽象层 */
    RET_MOD_RB   = 0x04u, /* RingBuffer */
    RET_MOD_MEM  = 0x05u, /* MemPool / MemoryAllocation */
    RET_MOD_LOG  = 0x06u, /* 日志 */
    RET_MOD_AT   = 0x07u, /* AT/协议栈 */
    RET_MOD_PORT = 0x08u, /* 平台端口层 platform/ports */
} ret_module_id_t;

/* -----------------------------------------------------------------------------
 * Errno（低 24 位）
 * 说明：这里的值是“错误原因”，与模块无关；模块由高 8 位区分。
 * -----------------------------------------------------------------------------
 */
typedef enum {
    RET_ERRNO_OK              = 0u, /* 成功 */

    RET_ERRNO_FAIL            = 1u,  /* 未分类失败 */
    RET_ERRNO_INVALID_ARG     = 2u,  /* 参数非法 */
    RET_ERRNO_TIMEOUT         = 3u,  /* 超时 */
    RET_ERRNO_BUSY            = 4u,  /* 忙/资源占用 */
    RET_ERRNO_NO_MEM          = 5u,  /* 内存不足（注意：本工程禁 malloc，通常指内存池不足） */
    RET_ERRNO_NOT_FOUND       = 6u,  /* 未找到 */
    RET_ERRNO_NOT_READY       = 7u,  /* 未就绪 */
    RET_ERRNO_DATA_OVERFLOW   = 8u,  /* 数据溢出 */
    RET_ERRNO_UNSUPPORTED     = 9u,  /* 不支持 */
    RET_ERRNO_IO              = 10u, /* IO 错误 */
    RET_ERRNO_CRC             = 11u, /* CRC 错误 */
    RET_ERRNO_DATA_NOT_ENOUGH = 12u, /* 数据不足（比如解析需要更多字节） */
    RET_ERRNO_ISR             = 13u, /* 不允许在 ISR 调用/ISR 语境错误 */
} ret_errno_t;

/* -----------------------------------------------------------------------------
 * 打包/解包宏
 * -----------------------------------------------------------------------------
 */

/* 组装错误码：模块ID + errno */
#define RET_MAKE(mod, errno_) \
    ((ret_code_t)((((uint32_t)(mod) & 0xFFu) << 24) | ((uint32_t)(errno_) & 0x00FFFFFFu)))

/* 提取模块 ID（高 8 位） */
#define RET_MODULE(rc) ((uint8_t)((((uint32_t)(rc)) >> 24) & 0xFFu))

/* 提取 errno（低 24 位） */
#define RET_ERRNO(rc) ((uint32_t)(((uint32_t)(rc)) & 0x00FFFFFFu))

/* -----------------------------------------------------------------------------
 * 成功码
 * -----------------------------------------------------------------------------
 */
#define RET_OK ((ret_code_t)0u)

/* =============================================================================
 * 兼容层：保留旧错误名（RET_E_xxx）
 *
 * 注意：
 *   这些旧名字统一归到 CORE 模块（RET_MOD_CORE）。
 *   若你后续让各模块返回“自己的模块 ID”，上层逻辑不应再写：
 *     if (rc == RET_E_TIMEOUT) ...
 *   而应改成：
 *     if (ret_is_timeout(rc)) ...
 *   这样即使模块 ID 不同，也能正确判断“超时”这一类错误。
 * =============================================================================
 */
#define RET_E_FAIL            RET_MAKE(RET_MOD_CORE, RET_ERRNO_FAIL)
#define RET_E_INVALID_ARG     RET_MAKE(RET_MOD_CORE, RET_ERRNO_INVALID_ARG)
#define RET_E_TIMEOUT         RET_MAKE(RET_MOD_CORE, RET_ERRNO_TIMEOUT)
#define RET_E_BUSY            RET_MAKE(RET_MOD_CORE, RET_ERRNO_BUSY)
#define RET_E_NO_MEM          RET_MAKE(RET_MOD_CORE, RET_ERRNO_NO_MEM)
#define RET_E_NOT_FOUND       RET_MAKE(RET_MOD_CORE, RET_ERRNO_NOT_FOUND)
#define RET_E_NOT_READY       RET_MAKE(RET_MOD_CORE, RET_ERRNO_NOT_READY)
#define RET_E_DATA_OVERFLOW   RET_MAKE(RET_MOD_CORE, RET_ERRNO_DATA_OVERFLOW)
#define RET_E_UNSUPPORTED     RET_MAKE(RET_MOD_CORE, RET_ERRNO_UNSUPPORTED)
#define RET_E_IO              RET_MAKE(RET_MOD_CORE, RET_ERRNO_IO)
#define RET_E_CRC             RET_MAKE(RET_MOD_CORE, RET_ERRNO_CRC)
#define RET_E_DATA_NOT_ENOUGH RET_MAKE(RET_MOD_CORE, RET_ERRNO_DATA_NOT_ENOUGH)
#define RET_E_ISR             RET_MAKE(RET_MOD_CORE, RET_ERRNO_ISR)

/* =============================================================================
 * 状态判断
 * =============================================================================
 */
CORE_INLINE bool ret_is_ok(ret_code_t r) {
    return (r == RET_OK);
}

CORE_INLINE bool ret_is_err(ret_code_t r) {
    return (r != RET_OK);
}

/* =============================================================================
 * 基于 errno 的判断（与模块无关，推荐上层使用）
 * =============================================================================
 */
CORE_INLINE bool ret_is_errno(ret_code_t r, ret_errno_t e) {
    return (RET_ERRNO(r) == (uint32_t)e);
}

/* 常用分类判断：上层用它们可保持兼容（即使模块ID不同） */
CORE_INLINE bool ret_is_timeout(ret_code_t r) {
    return ret_is_errno(r, RET_ERRNO_TIMEOUT);
}
CORE_INLINE bool ret_is_busy(ret_code_t r) {
    return ret_is_errno(r, RET_ERRNO_BUSY);
}
CORE_INLINE bool ret_is_no_mem(ret_code_t r) {
    return ret_is_errno(r, RET_ERRNO_NO_MEM);
}
CORE_INLINE bool ret_is_invalid_arg(ret_code_t r) {
    return ret_is_errno(r, RET_ERRNO_INVALID_ARG);
}

#ifdef __cplusplus
}
#endif

#endif /* SMARTLOCK_RET_CODE_H */
