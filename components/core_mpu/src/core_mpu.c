#include "core_mpu.h"

#include "core_mpu_port.h"

/*
 * 本模块刻意不强制包含 CMSIS core 头文件：
 * - 可直接使用 Arm 对 Cortex‑M 规定的固定系统寄存器地址进行配置。
 * - 是否“存在 MPU”由宏决定；若未启用则编译为 no-op。
 *
 * 重要说明（解决“找不到 __MPU_PRESENT”）：
 * - 标准做法：由设备头文件（如 STM32 的 device header）定义 `__MPU_PRESENT`。
 * - 若你的工程没有包含/没有定义该宏，可在编译选项添加：
 *     -DCORE_MPU_ASSUME_PRESENT=1
 *   组件会把它当作 `__MPU_PRESENT` 的兜底值。
 */

#ifndef CORE_MPU_MIN_REGION_SIZE
/* MPU region 的最小粒度（Cortex‑M MPU 最小 32B，对齐/边界计算都基于它）。 */
#define CORE_MPU_MIN_REGION_SIZE 32u
#endif

#ifndef CORE_MPU_NULL_TRAP_SIZE
/* NULL trap 覆盖大小（仅 PMSAv7 生效）：用于捕获 0x0 附近的空指针访问。 */
#define CORE_MPU_NULL_TRAP_SIZE 1024u
#endif

#ifndef CORE_MPU_DEFAULT_STACK_GUARD_SIZE
/* 默认栈保护大小（仅 PMSAv7 生效）：必须为 2 的幂且 >= 32。 */
#define CORE_MPU_DEFAULT_STACK_GUARD_SIZE 32u
#endif

#ifndef CORE_MPU_DEFAULT_PERIPH_BASE
/* 默认外设地址空间基址（常见 Cortex‑M 外设区：0x4000_0000 起）。 */
#define CORE_MPU_DEFAULT_PERIPH_BASE ((uintptr_t)0x40000000u)
#endif

#ifndef CORE_MPU_DEFAULT_PERIPH_SIZE
/* 默认外设地址空间大小（常见：512MiB，要求为 2 的幂且基址对齐）。 */
#define CORE_MPU_DEFAULT_PERIPH_SIZE ((size_t)0x20000000u) /* 512 MiB */
#endif

/* 兜底：有些工程未定义 __MPU_PRESENT。 */
#if !defined(__MPU_PRESENT)
#if defined(CORE_MPU_ASSUME_PRESENT)
/* 用户显式指定“认为 MPU 存在”。典型用于未引入 device/CMSIS 头但芯片确认有 MPU 的场景。 */
#define __MPU_PRESENT CORE_MPU_ASSUME_PRESENT
#else
/* 未定义则默认认为无 MPU（安全降级为 no-op）。 */
#define __MPU_PRESENT 0
#endif
#endif

#if defined(__MPU_PRESENT) && (__MPU_PRESENT == 1)
/* 编译期判定：当前目标支持 MPU 配置。 */
#define CORE_MPU_HAS_MPU 1
#else
/* 编译期判定：无 MPU（或未开启），本模块会编译为 no-op。 */
#define CORE_MPU_HAS_MPU 0
#endif

#if CORE_MPU_HAS_MPU

/* -------------------------------------------------------------------------- */
/* 最小系统寄存器映射（Cortex‑M System Control Space）                          */
/* -------------------------------------------------------------------------- */

#define CORE_MPU_SCS_BASE       0xE000E000u
#define CORE_MPU_SCB_SHCSR_ADDR (CORE_MPU_SCS_BASE + 0x0D24u)
#define CORE_MPU_MPU_ADDR       (CORE_MPU_SCS_BASE + 0x0D90u)

#define CORE_MPU_SCB_SHCSR_MEMFAULTENA_Msk (1u << 16)

static inline void core_mpu_dmb(void) {
#if defined(__arm__) || defined(__thumb__) || defined(_M_ARM) || defined(__ARM_ARCH)
#if defined(__GNUC__) || defined(__clang__)
    __asm volatile("dmb 0xF" ::: "memory");
#elif defined(__ICCARM__)
    __asm("DMB");
#else
    __asm volatile("dmb 0xF" ::: "memory");
#endif
#else
    /* 非 ARM 架构：保持为空 */
#endif
}

static inline void core_mpu_dsb(void) {
#if defined(__arm__) || defined(__thumb__) || defined(_M_ARM) || defined(__ARM_ARCH)
#if defined(__GNUC__) || defined(__clang__)
    __asm volatile("dsb 0xF" ::: "memory");
#elif defined(__ICCARM__)
    __asm("DSB");
#else
    __asm volatile("dsb 0xF" ::: "memory");
#endif
#else
    /* 非 ARM 架构：保持为空 */
#endif
}

static inline void core_mpu_isb(void) {
#if defined(__arm__) || defined(__thumb__) || defined(_M_ARM) || defined(__ARM_ARCH)
#if defined(__GNUC__) || defined(__clang__)
    __asm volatile("isb 0xF" ::: "memory");
#elif defined(__ICCARM__)
    __asm("ISB");
#else
    __asm volatile("isb 0xF" ::: "memory");
#endif
#else
    /* 非 ARM 架构：保持为空 */
#endif
}

static inline volatile uint32_t* core_mpu_scb_shcsr(void) {
    return (volatile uint32_t*)(uintptr_t)CORE_MPU_SCB_SHCSR_ADDR;
}

/* -------------------------------------------------------------------------- */
/* MPU 寄存器布局                                                              */
/* -------------------------------------------------------------------------- */

#if defined(CORE_MPU_USE_PMSAV8)
/* 强制选择 PMSAv8（一般用于 M33/M55 等 v8M 架构，使用 RBAR/RLAR/MAIR）。 */
#define CORE_MPU_PMSAV8 1
#elif defined(CORE_MPU_USE_PMSAV7)
/* 强制选择 PMSAv7（一般用于 M3/M4/M7 等 v7M 架构，使用 RBAR/RASR）。 */
#define CORE_MPU_PMSAV8 0
#elif defined(__ARM_ARCH_8M_MAIN__) || defined(__ARM_ARCH_8M_BASE__)
/* 自动推断：ARMv8‑M 架构默认使用 PMSAv8。 */
#define CORE_MPU_PMSAV8 1
#else
/* 其它情况默认 PMSAv7（对常见 F4/F7/M7 更匹配）。 */
#define CORE_MPU_PMSAV8 0
#endif

typedef struct {
    volatile uint32_t TYPE;
    volatile uint32_t CTRL;
    volatile uint32_t RNR;
    volatile uint32_t RBAR;
#if CORE_MPU_PMSAV8
    volatile uint32_t RLAR;
    volatile uint32_t RBAR_A1;
    volatile uint32_t RLAR_A1;
    volatile uint32_t RBAR_A2;
    volatile uint32_t RLAR_A2;
    volatile uint32_t RBAR_A3;
    volatile uint32_t RLAR_A3;
    uint32_t RESERVED0[1];
    volatile uint32_t MAIR0;
    volatile uint32_t MAIR1;
#else
    volatile uint32_t RASR;
#endif
} core_mpu_regs_t;

static inline core_mpu_regs_t* core_mpu_regs(void) {
    return (core_mpu_regs_t*)(uintptr_t)CORE_MPU_MPU_ADDR;
}

static inline uint32_t core_mpu_hw_regions(void) {
    /* TYPE.DREGION 位域在 PMSAv7 与 PMSAv8 都是 [15:8]。 */
    return (core_mpu_regs()->TYPE >> 8) & 0xFFu;
}

/* -------------------------------------------------------------------------- */
/* 通用工具函数                                                                */
/* -------------------------------------------------------------------------- */
/**
 * @brief 判断改制是否是 2的幂次
 * @param x 数值
 * @return 状态码
 *
 */
static inline bool core_mpu_is_pow2_u32(uint32_t x) {
    return (x != 0u) && ((x & (x - 1u)) == 0u);
}

/**
 * @brief 判断该值是否是
 * @param value 地址
 * @param alignment 对齐位数
 * @return 状态码
 */
static inline bool core_mpu_is_aligned(uintptr_t value, uintptr_t alignment) {
    return (alignment != 0u) && ((value & (alignment - 1u)) == 0u);
}

static uint32_t core_mpu_floor_pow2_u32(uint32_t x) {
    if (x == 0u) return 0u;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x - (x >> 1);
}

static core_mpu_rc_t core_mpu_validate_map(const core_mpu_map_t* m) {
    if (!m) return CORE_MPU_RC_INVALID_ARG;

    if (m->flash_start >= m->flash_end) return CORE_MPU_RC_INVALID_ARG;
    if (m->ram_start >= m->ram_end) return CORE_MPU_RC_INVALID_ARG;
    if (m->stack_bottom >= m->stack_top) return CORE_MPU_RC_INVALID_ARG;

    /* 最小粒度要求（所有 start/end 必须 32B 对齐）。 */
    if (!core_mpu_is_aligned(m->flash_start, CORE_MPU_MIN_REGION_SIZE))
        return CORE_MPU_RC_INVALID_ARG;
    if (!core_mpu_is_aligned(m->flash_end, CORE_MPU_MIN_REGION_SIZE))
        return CORE_MPU_RC_INVALID_ARG;
    if (!core_mpu_is_aligned(m->ram_start, CORE_MPU_MIN_REGION_SIZE))
        return CORE_MPU_RC_INVALID_ARG;
    if (!core_mpu_is_aligned(m->ram_end, CORE_MPU_MIN_REGION_SIZE)) return CORE_MPU_RC_INVALID_ARG;
    if (!core_mpu_is_aligned(m->stack_bottom, CORE_MPU_MIN_REGION_SIZE))
        return CORE_MPU_RC_INVALID_ARG;
    if (!core_mpu_is_aligned(m->stack_top, CORE_MPU_MIN_REGION_SIZE))
        return CORE_MPU_RC_INVALID_ARG;

    /* 栈范围必须落在 RAM 内。 */
    if ((m->stack_bottom < m->ram_start) || (m->stack_top > m->ram_end))
        return CORE_MPU_RC_INVALID_ARG;

    return CORE_MPU_RC_OK;
}

static uint32_t core_mpu_count_pow2_regions(uintptr_t start, uintptr_t end) {
    if (start >= end) return 0u;
    if (!core_mpu_is_aligned(start, CORE_MPU_MIN_REGION_SIZE)) return 0u;
    if (!core_mpu_is_aligned(end, CORE_MPU_MIN_REGION_SIZE)) return 0u;

    uint32_t count = 0;
    uintptr_t cur  = start;
    while (cur < end) {
        const uint32_t remaining = (uint32_t)(end - cur);
        uint32_t size            = core_mpu_floor_pow2_u32(remaining);
        if (size < CORE_MPU_MIN_REGION_SIZE) size = CORE_MPU_MIN_REGION_SIZE;
        while (!core_mpu_is_aligned(cur, (uintptr_t)size)) {
            size >>= 1;
            if (size < CORE_MPU_MIN_REGION_SIZE) return 0u;
        }
        cur += (uintptr_t)size;
        count++;
    }
    return count;
}

/* -------------------------------------------------------------------------- */
/* PMSAv7（RASR）实现                                                           */
/* -------------------------------------------------------------------------- */

#if !CORE_MPU_PMSAV8

/* CTRL bits */
#define CORE_MPU_V7_CTRL_ENABLE_Msk     (1u << 0)
#define CORE_MPU_V7_CTRL_HFNMIENA_Msk   (1u << 1)
#define CORE_MPU_V7_CTRL_PRIVDEFENA_Msk (1u << 2)

/* RASR fields */
#define CORE_MPU_V7_RASR_ENABLE_Msk (1u << 0)
#define CORE_MPU_V7_RASR_SIZE_Pos   1u
#define CORE_MPU_V7_RASR_SRD_Pos    8u
#define CORE_MPU_V7_RASR_B_Pos      16u
#define CORE_MPU_V7_RASR_C_Pos      17u
#define CORE_MPU_V7_RASR_S_Pos      18u
#define CORE_MPU_V7_RASR_TEX_Pos    19u
#define CORE_MPU_V7_RASR_AP_Pos     24u
#define CORE_MPU_V7_RASR_XN_Pos     28u

/* RBAR address mask (minimum 32-byte alignment) */
#define CORE_MPU_V7_RBAR_ADDR_Msk 0xFFFFFFE0u

static uint32_t core_mpu_v7_size_encode(uint32_t size) {
    /* SIZE = log2(size) - 1，且 size >= 32、并且为 2 的幂。 */
    uint32_t log2 = 0;
    while ((1u << log2) < size) log2++;
    return (log2 - 1u) << CORE_MPU_V7_RASR_SIZE_Pos;
}

static core_mpu_rc_t core_mpu_v7_set_region(uint32_t region, uintptr_t base, uint32_t size,
                                            uint32_t rasr_attr) {
    if (!core_mpu_is_pow2_u32(size)) return CORE_MPU_RC_INVALID_ARG;
    if (size < CORE_MPU_MIN_REGION_SIZE) return CORE_MPU_RC_INVALID_ARG;
    if (!core_mpu_is_aligned(base, (uintptr_t)size)) return CORE_MPU_RC_INVALID_ARG;

    core_mpu_regs_t* mpu = core_mpu_regs();
    mpu->RNR             = region;
    mpu->RBAR            = ((uint32_t)base) & CORE_MPU_V7_RBAR_ADDR_Msk;
    mpu->RASR            = rasr_attr | core_mpu_v7_size_encode(size) | CORE_MPU_V7_RASR_ENABLE_Msk;
    return CORE_MPU_RC_OK;
}

static core_mpu_rc_t core_mpu_v7_program_range(uint32_t* region_io, uint32_t region_limit,
                                               uintptr_t start, uintptr_t end, uint32_t rasr_attr) {
    if (start >= end) return CORE_MPU_RC_OK;
    if (!region_io) return CORE_MPU_RC_INVALID_ARG;

    uintptr_t cur = start;
    while (cur < end) {
        const uint32_t remaining = (uint32_t)(end - cur);
        uint32_t size            = core_mpu_floor_pow2_u32(remaining);
        if (size < CORE_MPU_MIN_REGION_SIZE) size = CORE_MPU_MIN_REGION_SIZE;
        while (!core_mpu_is_aligned(cur, (uintptr_t)size)) {
            size >>= 1;
            if (size < CORE_MPU_MIN_REGION_SIZE) return CORE_MPU_RC_INTERNAL;
        }

        const uint32_t region = *region_io;
        if (region >= region_limit) return CORE_MPU_RC_NO_RESOURCE;

        core_mpu_rc_t rc = core_mpu_v7_set_region(region, cur, size, rasr_attr);
        if (rc != CORE_MPU_RC_OK) return rc;

        *region_io = region + 1u;
        cur += (uintptr_t)size;
    }

    return CORE_MPU_RC_OK;
}

static uint32_t core_mpu_v7_attr_normal(bool xn, uint32_t ap) {
    /* 常见 Normal（WB/WA）编码：TEX=0, C=1, B=1, S=0。 */
    const uint32_t XN  = (xn ? 1u : 0u) << CORE_MPU_V7_RASR_XN_Pos;
    const uint32_t AP  = (ap & 0x7u) << CORE_MPU_V7_RASR_AP_Pos;
    const uint32_t TEX = 0u << CORE_MPU_V7_RASR_TEX_Pos;
    const uint32_t C   = 1u << CORE_MPU_V7_RASR_C_Pos;
    const uint32_t B   = 1u << CORE_MPU_V7_RASR_B_Pos;
    const uint32_t S   = 0u << CORE_MPU_V7_RASR_S_Pos;
    return XN | AP | TEX | C | B | S;
}

static uint32_t core_mpu_v7_attr_device(bool xn, uint32_t ap) {
    /* 常见 Device 编码：TEX=2, C=0, B=0, S=1。 */
    const uint32_t XN  = (xn ? 1u : 0u) << CORE_MPU_V7_RASR_XN_Pos;
    const uint32_t AP  = (ap & 0x7u) << CORE_MPU_V7_RASR_AP_Pos;
    const uint32_t TEX = 2u << CORE_MPU_V7_RASR_TEX_Pos;
    const uint32_t C   = 0u << CORE_MPU_V7_RASR_C_Pos;
    const uint32_t B   = 0u << CORE_MPU_V7_RASR_B_Pos;
    const uint32_t S   = 1u << CORE_MPU_V7_RASR_S_Pos;
    return XN | AP | TEX | C | B | S;
}

static core_mpu_rc_t core_mpu_apply_default_v7(const core_mpu_map_t* map,
                                               const core_mpu_policy_t* p) {
    core_mpu_regs_t* mpu = core_mpu_regs();

    /* 开启 MemManage fault，用于捕获 MPU 违规。 */
    *core_mpu_scb_shcsr() |= CORE_MPU_SCB_SHCSR_MEMFAULTENA_Msk;

    core_mpu_dmb();
    mpu->CTRL = 0u; /* 配置期间先关闭 MPU。 */
    core_mpu_dsb();
    core_mpu_isb();

    const uint32_t hw_regions = core_mpu_hw_regions();
    if (hw_regions == 0u) return CORE_MPU_RC_UNSUPPORTED;

    /* 构建保守布局：优先正确性，避免“过度覆盖”导致的难排查问题。 */
    uint32_t needed_regions = 0u;
    needed_regions += (p->enable_null_trap ? 1u : 0u);
    needed_regions += core_mpu_count_pow2_regions(map->flash_start, map->flash_end);
    needed_regions += core_mpu_count_pow2_regions(map->ram_start, map->ram_end);
    needed_regions += (p->enable_stack_guard ? 1u : 0u);
    needed_regions += (p->enable_peripheral_region ? 1u : 0u);

    bool use_fallback_coarse = false;
    if (needed_regions == 0u) use_fallback_coarse = true;
    if (needed_regions > hw_regions) use_fallback_coarse = true;

    uint32_t region = 0u;

    /* 可选：NULL trap（仅当你的系统不需要使用 0x0 映射时才安全）。 */
    if (p->enable_null_trap) {
        if (region >= hw_regions) return CORE_MPU_RC_NO_RESOURCE;
        (void)core_mpu_v7_set_region(region++, 0x00000000u, CORE_MPU_NULL_TRAP_SIZE,
                                     core_mpu_v7_attr_normal(true, 0u /* no access */));
    }

    if (!use_fallback_coarse) {
        core_mpu_rc_t rc =
            core_mpu_v7_program_range(&region, hw_regions, map->flash_start, map->flash_end,
                                      core_mpu_v7_attr_normal(false, 6u /* RO */));
        if (rc != CORE_MPU_RC_OK) return rc;

        rc = core_mpu_v7_program_range(&region, hw_regions, map->ram_start, map->ram_end,
                                       core_mpu_v7_attr_normal(true, 3u /* RW */));
        if (rc != CORE_MPU_RC_OK) return rc;
    } else {
        /* 粗粒度兜底：Flash 1 个 region + RAM 1 个 region。 */
        const uint32_t flash_size = (uint32_t)(map->flash_end - map->flash_start);
        const uint32_t ram_size   = (uint32_t)(map->ram_end - map->ram_start);

        uint32_t flash_rsize      = core_mpu_floor_pow2_u32(flash_size);
        if (flash_rsize < flash_size) flash_rsize <<= 1;
        uint32_t ram_rsize = core_mpu_floor_pow2_u32(ram_size);
        if (ram_rsize < ram_size) ram_rsize <<= 1;

        if (!core_mpu_is_aligned(map->flash_start, flash_rsize)) return CORE_MPU_RC_NO_RESOURCE;
        if (!core_mpu_is_aligned(map->ram_start, ram_rsize)) return CORE_MPU_RC_NO_RESOURCE;

        if ((region + 2u) > hw_regions) return CORE_MPU_RC_NO_RESOURCE;
        core_mpu_rc_t rc = core_mpu_v7_set_region(region++, map->flash_start, flash_rsize,
                                                  core_mpu_v7_attr_normal(false, 6u /* RO */));
        if (rc != CORE_MPU_RC_OK) return rc;
        rc = core_mpu_v7_set_region(region++, map->ram_start, ram_rsize,
                                    core_mpu_v7_attr_normal(true, 3u /* RW */));
        if (rc != CORE_MPU_RC_OK) return rc;
    }

    /* 可选：stack guard（需要比 RAM region 更高优先级）。 */
    if (p->enable_stack_guard) {
        const uint32_t guard_size =
            (p->stack_guard_size != 0u) ? p->stack_guard_size : CORE_MPU_DEFAULT_STACK_GUARD_SIZE;
        if (!core_mpu_is_pow2_u32(guard_size) || (guard_size < CORE_MPU_MIN_REGION_SIZE)) {
            return CORE_MPU_RC_INVALID_ARG;
        }
        if (!core_mpu_is_aligned(map->stack_bottom, guard_size)) return CORE_MPU_RC_INVALID_ARG;
        if ((map->stack_bottom + guard_size) > map->stack_top) return CORE_MPU_RC_INVALID_ARG;
        if (region >= hw_regions) return CORE_MPU_RC_NO_RESOURCE;

        core_mpu_rc_t rc =
            core_mpu_v7_set_region(region++, map->stack_bottom, guard_size,
                                   core_mpu_v7_attr_normal(true, 0u /* no access */));
        if (rc != CORE_MPU_RC_OK) return rc;
    }

    /* 可选：外设 region（device memory）。 */
    if (p->enable_peripheral_region) {
        const uintptr_t base =
            (p->peripheral_base != 0u) ? p->peripheral_base : CORE_MPU_DEFAULT_PERIPH_BASE;
        const size_t size =
            (p->peripheral_size != 0u) ? p->peripheral_size : CORE_MPU_DEFAULT_PERIPH_SIZE;
        if (!core_mpu_is_pow2_u32((uint32_t)size)) return CORE_MPU_RC_INVALID_ARG;
        if (!core_mpu_is_aligned(base, (uintptr_t)size)) return CORE_MPU_RC_INVALID_ARG;
        if (region >= hw_regions) return CORE_MPU_RC_NO_RESOURCE;

        core_mpu_rc_t rc = core_mpu_v7_set_region(region++, base, (uint32_t)size,
                                                  core_mpu_v7_attr_device(true, 3u /* RW */));
        if (rc != CORE_MPU_RC_OK) return rc;
    }

    /* 使能 MPU。 */
    uint32_t ctrl = CORE_MPU_V7_CTRL_ENABLE_Msk;
    if (p->enable_mpu_in_faults) ctrl |= CORE_MPU_V7_CTRL_HFNMIENA_Msk;
    if (p->enable_privileged_default_map) ctrl |= CORE_MPU_V7_CTRL_PRIVDEFENA_Msk;

    mpu->CTRL = ctrl;
    core_mpu_dsb();
    core_mpu_isb();
    return CORE_MPU_RC_OK;
}

#endif /* !CORE_MPU_PMSAV8 */

/* -------------------------------------------------------------------------- */
/* PMSAv8（RLAR/MAIR）实现                                                      */
/* -------------------------------------------------------------------------- */

#if CORE_MPU_PMSAV8

/* CTRL bits (same positions as PMSAv7) */
#define CORE_MPU_V8_CTRL_ENABLE_Msk     (1u << 0)
#define CORE_MPU_V8_CTRL_HFNMIENA_Msk   (1u << 1)
#define CORE_MPU_V8_CTRL_PRIVDEFENA_Msk (1u << 2)

/* RBAR fields */
#define CORE_MPU_V8_RBAR_BASE_Msk 0xFFFFFFE0u
#define CORE_MPU_V8_RBAR_SH_Pos   3u
#define CORE_MPU_V8_RBAR_AP_Pos   1u
#define CORE_MPU_V8_RBAR_XN_Pos   0u

/* RLAR fields */
#define CORE_MPU_V8_RLAR_LIMIT_Msk    0xFFFFFFE0u
#define CORE_MPU_V8_RLAR_ATTRINDX_Pos 1u
#define CORE_MPU_V8_RLAR_EN_Msk       (1u << 0)

/* MAIR attribute encoding: 8-bit per index */
#define CORE_MPU_V8_MAIR_ATTR_DEVICE_nGnRnE 0x00u
#define CORE_MPU_V8_MAIR_ATTR_NORMAL_NC     0x44u
#define CORE_MPU_V8_MAIR_ATTR_NORMAL_WB_WA  0xFFu

static void core_mpu_v8_set_mair_defaults(void) {
    /* AttrIdx 0：device；AttrIdx 1：normal WB/WA；AttrIdx 2：normal non-cacheable。 */
    core_mpu_regs_t* mpu = core_mpu_regs();
    const uint32_t mair0 = ((uint32_t)CORE_MPU_V8_MAIR_ATTR_DEVICE_nGnRnE << (0u * 8u)) |
                           ((uint32_t)CORE_MPU_V8_MAIR_ATTR_NORMAL_WB_WA << (1u * 8u)) |
                           ((uint32_t)CORE_MPU_V8_MAIR_ATTR_NORMAL_NC << (2u * 8u));
    mpu->MAIR0 = mair0;
    mpu->MAIR1 = 0u;
}

static core_mpu_rc_t core_mpu_v8_set_region(uint32_t region, uintptr_t base,
                                            uintptr_t limit_inclusive, uint32_t attr_index,
                                            uint32_t ap, uint32_t sh, bool xn) {
    if (!core_mpu_is_aligned(base, CORE_MPU_MIN_REGION_SIZE)) return CORE_MPU_RC_INVALID_ARG;
    if (!core_mpu_is_aligned(limit_inclusive + 1u, CORE_MPU_MIN_REGION_SIZE))
        return CORE_MPU_RC_INVALID_ARG;
    if (limit_inclusive < base) return CORE_MPU_RC_INVALID_ARG;

    core_mpu_regs_t* mpu = core_mpu_regs();
    mpu->RNR             = region;

    const uint32_t rbar =
        (((uint32_t)base) & CORE_MPU_V8_RBAR_BASE_Msk) | ((sh & 0x3u) << CORE_MPU_V8_RBAR_SH_Pos) |
        ((ap & 0x3u) << CORE_MPU_V8_RBAR_AP_Pos) | ((xn ? 1u : 0u) << CORE_MPU_V8_RBAR_XN_Pos);
    const uint32_t rlar = (((uint32_t)limit_inclusive) & CORE_MPU_V8_RLAR_LIMIT_Msk) |
                          ((attr_index & 0x7u) << CORE_MPU_V8_RLAR_ATTRINDX_Pos) |
                          CORE_MPU_V8_RLAR_EN_Msk;

    mpu->RBAR = rbar;
    mpu->RLAR = rlar;
    return CORE_MPU_RC_OK;
}

static core_mpu_rc_t core_mpu_apply_default_v8(const core_mpu_map_t* map,
                                               const core_mpu_policy_t* p) {
    core_mpu_regs_t* mpu = core_mpu_regs();

    *core_mpu_scb_shcsr() |= CORE_MPU_SCB_SHCSR_MEMFAULTENA_Msk;

    core_mpu_dmb();
    mpu->CTRL = 0u;
    core_mpu_dsb();
    core_mpu_isb();

    const uint32_t hw_regions = core_mpu_hw_regions();
    if (hw_regions == 0u) return CORE_MPU_RC_UNSUPPORTED;

    core_mpu_v8_set_mair_defaults();

    uint32_t region = 0u;

    /* Flash：只读、可执行、normal memory（WB/WA）。 */
    if (region >= hw_regions) return CORE_MPU_RC_NO_RESOURCE;
    core_mpu_rc_t rc = core_mpu_v8_set_region(
        region++, map->flash_start, map->flash_end - 1u, 1u /* normal WB/WA */,
        3u /* RO for both (implementation-defined on some parts) */, 0u /* non-shareable */, false);
    if (rc != CORE_MPU_RC_OK) return rc;

    /* RAM：读写、XN、normal memory（WB/WA）。 */
    if (region >= hw_regions) return CORE_MPU_RC_NO_RESOURCE;
    rc = core_mpu_v8_set_region(region++, map->ram_start, map->ram_end - 1u, 1u /* normal WB/WA */,
                                1u /* RW for both (implementation-defined on some parts) */, 0u,
                                true);
    if (rc != CORE_MPU_RC_OK) return rc;

    if (p->enable_peripheral_region) {
        const uintptr_t base =
            (p->peripheral_base != 0u) ? p->peripheral_base : CORE_MPU_DEFAULT_PERIPH_BASE;
        const size_t size =
            (p->peripheral_size != 0u) ? p->peripheral_size : CORE_MPU_DEFAULT_PERIPH_SIZE;
        if (!core_mpu_is_aligned(base, CORE_MPU_MIN_REGION_SIZE)) return CORE_MPU_RC_INVALID_ARG;
        if (!core_mpu_is_aligned((uintptr_t)size, CORE_MPU_MIN_REGION_SIZE))
            return CORE_MPU_RC_INVALID_ARG;
        if (region >= hw_regions) return CORE_MPU_RC_NO_RESOURCE;

        rc = core_mpu_v8_set_region(region++, base, (base + (uintptr_t)size) - 1u, 0u /* device */,
                                    1u /* RW */, 2u /* outer-shareable (typical for device) */,
                                    true);
        if (rc != CORE_MPU_RC_OK) return rc;
    }

    /*
     * PMSAv8 与 PMSAv7 在权限/无访问控制语义上存在差异。
     * 为保证可移植与安全性，这里默认不实现 null-trap/stack-guard（避免引入错误假设）。
     */

    uint32_t ctrl = CORE_MPU_V8_CTRL_ENABLE_Msk;
    if (p->enable_mpu_in_faults) ctrl |= CORE_MPU_V8_CTRL_HFNMIENA_Msk;
    if (p->enable_privileged_default_map) ctrl |= CORE_MPU_V8_CTRL_PRIVDEFENA_Msk;

    mpu->CTRL = ctrl;
    core_mpu_dsb();
    core_mpu_isb();
    return CORE_MPU_RC_OK;
}

#endif /* CORE_MPU_PMSAV8 */

#endif /* CORE_MPU_HAS_MPU */

bool core_mpu_is_supported(void) {
    return (CORE_MPU_HAS_MPU != 0);
}

uint32_t core_mpu_hw_region_count(void) {
#if CORE_MPU_HAS_MPU
    return core_mpu_hw_regions();
#else
    return 0u;
#endif
}

core_mpu_rc_t core_mpu_apply_default(const core_mpu_map_t* map, const core_mpu_policy_t* policy) {
    (void)policy;

#if !CORE_MPU_HAS_MPU
    (void)map;
    return CORE_MPU_RC_UNSUPPORTED;
#else
    core_mpu_rc_t rc = core_mpu_validate_map(map);
    if (rc != CORE_MPU_RC_OK) return rc;

    core_mpu_policy_t p = {
        .enable_privileged_default_map = true,
        .enable_mpu_in_faults          = true,
        .enable_null_trap = false, /* 默认关闭：很多 MCU 上 0x0 是有效 alias（向量表/Flash 映射） */
        .enable_stack_guard       = true,
        .stack_guard_size         = CORE_MPU_DEFAULT_STACK_GUARD_SIZE,
        .enable_peripheral_region = false,
        .peripheral_base          = CORE_MPU_DEFAULT_PERIPH_BASE,
        .peripheral_size          = CORE_MPU_DEFAULT_PERIPH_SIZE,
    };
    if (policy) p = *policy;

#if CORE_MPU_PMSAV8
    return core_mpu_apply_default_v8(map, &p);
#else
    return core_mpu_apply_default_v7(map, &p);
#endif
#endif
}
