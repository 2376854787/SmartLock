//
// Created by yan on 2025/12/20.
//

#ifndef SMARTLOCK_COMPILER_CUS_H
#define SMARTLOCK_COMPILER_CUS_H

#ifdef __cplusplus
extern "C" {



#endif

/* ================= Compiler detection =================
 * Support: GCC/Clang, ARMCLANG, ARMCC5, IAR
 */
#if defined(__ICCARM__)
#define COMPILER_IAR 1
#elif defined(__CC_ARM) && !defined(__ARMCC_VERSION)
/* Keil ARMCC5 defines __CC_ARM */
#define COMPILER_ARMCC5 1
#elif defined(__ARMCC_VERSION) && (__ARMCC_VERSION >= 6010050)
/* ARM Compiler 6 (armclang) */
#define COMPILER_ARMCLANG 1

#elif defined(__clang__)
/* clang (including clangd's parser) */
#define COMPILER_CLANG 1
#elif defined(__GNUC__)
#define COMPILER_GCC 1
#else
#error "Unsupported compiler"
#endif

/* ================= Basic keywords ================= */
#if defined(COMPILER_GCC) || defined(COMPILER_CLANG) || defined(COMPILER_ARMCLANG)

#ifndef __INLINE
#define __INLINE                static inline                                       /* 建议内联 */
#endif

#define   CORE_ALWAYS_INLINE    static inline __attribute__((always_inline))        /* 强烈要求内联 */
#define   CORE_INLINE           static inline                                       /* 建议内联 */
#define   CORE_NOINLINE        __attribute__((noinline))                            /* 禁止内联 */
#define   CORE_WEAK            __attribute__((weak))                                /* 弱定义允许覆盖 */

#ifndef   __PACKED
#define   __PACKED             __attribute__((packed))                              /* 结构体不填充 */
#endif

#define   CORE_ALIGNED(x)      __attribute__((aligned(x)))                          /* 指定对齐 */
#define   CORE_SECTION(x)      __attribute__((section(x)))                          /* 放到指定段 */
#define   CORE_USED            __attribute__((used))                                /* 禁止优化 */
#define   CORE_UNUSED          __attribute__((unused))                              /* 抑制未使用警告 */
#define   CORE_LIKELY(x)       __builtin_expect(!!(x), 1)                           /* 更可能为真 */
#define   CORE_UNLIKELY(x)     __builtin_expect(!!(x), 0)                           /* 更可能为假 */
#define   CORE_BARRIER()       __asm volatile ("" ::: "memory")                     /* 空编译指令  但带“memory” 内存可能被改不能把内存读写重排穿过它 */

#elif defined(COMPILER_ARMCC5)
#define __INLINE          static __inline
#define __CORE_INLINE     __inline
#define __NOINLINE        __attribute__((noinline))
#define __WEAK            __weak
#define __PACKED          __packed
#define __ALIGNED(x)      __align(x)
#define __SECTION(x)      __attribute__((section(x)))
#define __USED            __attribute__((used))
#define __UNUSED          __attribute__((unused))
#define __LIKELY(x)       (x)
#define __UNLIKELY(x)     (x)
#define __BARRIER()       __asm volatile ("" ::: "memory")
#elif defined(COMPILER_IAR)
#define __INLINE          static inline
#define __CORE_INLINE     inline
#define __NOINLINE        __attribute__((noinline))
#define __WEAK            __weak
#define __PACKED          __packed
#define __ALIGNED(x)      __attribute__((aligned(x)))
#define __SECTION(x)      __attribute__((section(x)))
#define __USED            __root
#define __UNUSED
#define __LIKELY(x)       (x)
#define __UNLIKELY(x)     (x)
#define __BARRIER()       __asm volatile ("" ::: "memory")
#endif

/* ================= Common helpers ================= */
#define CORE__UNUSED(x)      (void)(x)                                           /* 消除未使用警告 */

/* Static assert (C11 or fallback) */
#define CORE_STR_IMPL(x) #x
#define CORE_STR(x) CORE_STR_IMPL(x)

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#define CORE_STATIC_ASSERT(expr, msg) _Static_assert((expr), CORE_STR(msg))
#else
#define CORE_STATIC_ASSERT(expr, msg) typedef char static_assert_##msg[(expr) ? 1 : -1]
#endif

#ifdef __cplusplus
}
#endif

#endif //SMARTLOCK_COMPILER_CUS_H
