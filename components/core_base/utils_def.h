//
// Created by yan on 2025/12/20.
//

#ifndef SMARTLOCK_UTILS_DEF_H
#define SMARTLOCK_UTILS_DEF_H

#include <stddef.h>
#include <stdint.h>
#include "compiler_cus.h"

#ifdef __cplusplus
extern "C" {

#endif

/* ================= Basic math macros ================= */
#ifndef MIN
#define MIN(a, b)           (( (a) < (b) ) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b)           (( (a) > (b) ) ? (a) : (b))
#endif

#define CLAMP(x, lo, hi)    ( ((x) < (lo)) ? (lo) : (((x) > (hi)) ? (hi) : (x)) )

/* ================= Array helpers ================= */
#define ARRAY_SIZE(a)       (sizeof(a) / sizeof((a)[0]))

/* ================= Bit helpers ================= */
#define BIT(n)              (1UL << (n))
#define BIT_SET(x, n)       ((x) |= BIT(n))
#define BIT_CLR(x, n)       ((x) &= ~BIT(n))
#define BIT_GET(x, n)       (((x) >> (n)) & 0x1UL)

#define SET_BITS(reg, mask)     ((reg) |= (mask))
#define CLR_BITS(reg, mask)     ((reg) &= ~(mask))

/* ================= Align helpers ================= */
#define ALIGN_UP(x, a)      ( ((x) + ((a) - 1U)) & ~((a) - 1U) )
#define ALIGN_DOWN(x, a)    ( (x) & ~((a) - 1U) )

/* ================= container_of ================= */
#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif

#define container_of(ptr, type, member) \
((type *)((uint8_t *)(ptr) - offsetof(type, member)))

#ifdef __cplusplus
}
#endif

#endif //SMARTLOCK_UTILS_DEF_H
