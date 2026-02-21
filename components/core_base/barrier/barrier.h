#ifndef ARCH_BARRIER_H
#define ARCH_BARRIER_H

/* * 1. 定义通用语义接口
 * * ARCH_MEM_BARRIER:    保证内存访问顺序 (对应 DMB)
 * ARCH_SYNC_BARRIER:   保证硬件同步完成 (对应 DSB)
 * ARCH_INST_BARRIER:   刷新指令流水线   (对应 ISB)
 * ARCH_COMPILER_BARRIER: 仅防止编译器乱序 (软件屏障)
 */

// 声明接口
static inline void mem_barrier(void);
static inline void sync_barrier(void);
static inline void inst_barrier(void);
static inline void compiler_barrier(void);

/* ----------------------------------------------------------- */
/* 2. 具体的实现层  */
/* ----------------------------------------------------------- */

/* ===  ARM 架构  === */
#if defined(__arm__) || defined(__aarch64__)
__attribute__((unused)) static inline void mem_barrier(void) {
    __asm volatile("dmb 0xF" ::: "memory");
}

__attribute__((unused)) static inline void sync_barrier(void) {
    __asm volatile("dsb 0xF" ::: "memory");
}

__attribute__((unused)) static inline void inst_barrier(void) {
    __asm volatile("isb 0xF" ::: "memory");
}

/* ===  RISC-V 架构  === */
#elif defined(__riscv)

// RISC-V 的屏障指令 fence

static inline void mem_barrier(void) {
    // RISC-V 的读写屏障
    __asm volatile("fence" ::: "memory");
}

static inline void sync_barrier(void) {
    // RISC-V 通常 fence 就够了，或者 fence.i 用于指令流
    __asm volatile("fence" ::: "memory");
}

static inline void inst_barrier(void) {
    // 刷新指令缓存
    __asm volatile("fence.i" ::: "memory");
}

/* ===  x86  === */
#elif defined(__x86_64__) || defined(__i386__)

static inline void mem_barrier(void) {
    // x86 也就是 mfence
    __asm volatile("mfence" ::: "memory");
}

static inline void sync_barrier(void) {
    __asm volatile("mfence" ::: "memory");
}

static inline void inst_barrier(void) {
    // x86 流水线对用户通常透明，一般不需要手动刷，这里留空或用编译器屏障
    __asm volatile("" ::: "memory");
}

/* === 兜底 (未知架构) === */
#else
// 至少加上编译器屏障，防止编译器乱序
static inline void mem_barrier(void) {
    __asm volatile("" ::: "memory");
}
static inline void sync_barrier(void) {
    __asm volatile("" ::: "memory");
}
static inline void inst_barrier(void) {
    __asm volatile("" ::: "memory");
}
#endif

/* 编译器屏障（所有平台通用）：告诉 GCC 别优化这一行前后的内存访问 */
__attribute__((unused)) static inline void compiler_barrier(void) {
    __asm volatile("" ::: "memory");
}

#endif  // ARCH_BARRIER_H