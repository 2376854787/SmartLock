#ifndef SMARTLOCK_CORE_MPU_H
#define SMARTLOCK_CORE_MPU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * core_mpu：一个“可移植”的 MPU 默认配置组件
 *
 * 设计目标：
 * - 跨芯片：不同 Cortex‑M 芯片（有/无 MPU）都能编译，通过配置决定是否启用。
 * - 跨库：不强依赖 CMSIS 头文件（可直接使用固定系统寄存器地址进行配置）。
 * - 跨项目：通过 port hook 注入内存布局（可来自 linker 符号、手工配置等）。
 * - 跨架构：非 ARM 架构下可编译为 no-op（不会影响 PC/单测等构建）。
 */

typedef struct {
    uintptr_t flash_start; /* [start, end) */
    uintptr_t flash_end;
    uintptr_t ram_start; /* [start, end) */
    uintptr_t ram_end;
    uintptr_t stack_bottom; /* 栈完整范围：bottom < top */
    uintptr_t stack_top;
} core_mpu_map_t;

typedef enum {
    CORE_MPU_RC_OK = 0,
    CORE_MPU_RC_UNSUPPORTED,
    CORE_MPU_RC_INVALID_ARG,
    CORE_MPU_RC_NO_MAP,
    CORE_MPU_RC_NO_RESOURCE,
    CORE_MPU_RC_INTERNAL,
} core_mpu_rc_t;

typedef struct {
    /* 允许特权态使用默认内存映射（建议 bring-up 阶段开启）。 */
    bool enable_privileged_default_map;
    /* 在 HardFault/NMI 期间也启用 MPU 规则（安全项目建议开启）。 */
    bool enable_mpu_in_faults;

    /* 仅 PMSAv7：把 0x0 映射为 no-access，用于捕获空指针解引用。 */
    bool enable_null_trap;

    /*
     * 栈保护（stack guard）：
     * - 开启后：在 `stack_bottom` 放置一个 no-access region。
     * - `stack_guard_size` 必须为 2 的幂且 >= 32。
     */
    bool enable_stack_guard;
    uint32_t stack_guard_size;

    /* 可选：外设默认 region（RW、XN、Device）。 */
    bool enable_peripheral_region;
    uintptr_t peripheral_base;
    size_t peripheral_size;
} core_mpu_policy_t;

/* 平台 hook：提供有效的内存布局（返回 true 表示 out 填充成功且有效）。 */
bool core_mpu_port_get_map(core_mpu_map_t* out);

/* 当前构建/目标是否支持 MPU 配置（未启用/无 MPU 时返回 false）。 */
bool core_mpu_is_supported(void);

/* 返回硬件 MPU region 数量（不支持/不可用时返回 0）。 */
uint32_t core_mpu_hw_region_count(void);

/*
 * 基于 `map` 应用一套“默认”MPU 布局：
 * - Flash：只读，可执行
 * - RAM：读写，不可执行
 * - Peripherals（可选）：读写，不可执行，Device
 * - Null trap / stack guard（可选，仅 PMSAv7）：no-access
 */
core_mpu_rc_t core_mpu_apply_default(const core_mpu_map_t* map, const core_mpu_policy_t* policy);

#ifdef __cplusplus
}
#endif

#endif /* SMARTLOCK_CORE_MPU_H */
