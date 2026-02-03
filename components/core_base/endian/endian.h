#ifndef CORE_ENDIAN_H
#define CORE_ENDIAN_H
#include <stdint.h>

/* 编译期端序判断 优先用编译器宏 */
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && defined(__ORDER_BIG_ENDIAN__)
#define CORE_LITTLE_ENDIAN (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#else
/* 无法判断就强制就在编译选项里定义 CORE_CPU_LITTLE_ENDIAN=1/0 */
#if !defined(CORE_CPU_LITTLE_ENDIAN)
#error "Define CORE_CPU_LITTLE_ENDIAN (1=little,0=big)"
#endif
#define CORE_LITTLE_ENDIAN (CORE_CPU_LITTLE_ENDIAN)
#endif

static inline uint16_t core_bswap16(uint16_t x) {
    return (uint16_t)((x << 8) | (x >> 8));
}
static inline uint32_t core_bswap32(uint32_t x) {
    return ((x & 0x000000FFu) << 24) | ((x & 0x0000FF00u) << 8) | ((x & 0x00FF0000u) >> 8) |
           ((x & 0xFF000000u) >> 24);
}

static inline uint16_t core_htons(uint16_t x) {
    return CORE_LITTLE_ENDIAN ? core_bswap16(x) : x;
}
static inline uint32_t core_htonl(uint32_t x) {
    return CORE_LITTLE_ENDIAN ? core_bswap32(x) : x;
}
static inline uint16_t core_ntohs(uint16_t x) {
    return core_htons(x);
}
static inline uint32_t core_ntohl(uint32_t x) {
    return core_htonl(x);
}

/* 强制统一命名：业务层只用 htons/htonl/ntohs/ntohl */
#define htons(x) core_htons((uint16_t)(x))
#define htonl(x) core_htonl((uint32_t)(x))
#define ntohs(x) core_ntohs((uint16_t)(x))
#define ntohl(x) core_ntohl((uint32_t)(x))

/* 字节流辅助 */
static inline uint16_t read_be16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}
static inline uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static inline void write_be16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}
static inline void write_be32(uint8_t *p, uint32_t v) {
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

#endif