#ifndef ARCH_BARRIER_H
#define ARCH_BARRIER_H

/* * 1. 定义通用语义接口
 * * ARCH_MEM_BARRIER:    保证内存访问顺序 (对应 DMB)
 * ARCH_SYNC_BARRIER:   保证硬件同步完成 (对应 DSB)
 * ARCH_INST_BARRIER:   刷新指令流水线   (对应 ISB)
 * ARCH_COMPILER_BARRIER: 仅防止编译器乱序 (软件屏障)
 */

// 声明接口（具体的实现在下面根据宏自动选择）
static inline void mem_barrier(void);
static inline void sync_barrier(void);
static inline void inst_barrier(void);
static inline void compiler_barrier(void);

/* ----------------------------------------------------------- */
/* 2. 具体的实现层 (Implementation Layer)                      */
/* ----------------------------------------------------------- */

/* === 场景 A: 如果是 ARM 架构 (STM32, NXP 等) === */
#if defined(__arm__) || defined(__aarch64__)

// 为了做到“库无关”，我们直接嵌入汇编，不引用 cmsis.h

__attribute__((unused)) static inline void mem_barrier(void) {
    __asm volatile("dmb 0xF" ::: "memory");
}

__attribute__((unused)) static inline void sync_barrier(void) {
    __asm volatile("dsb 0xF" ::: "memory");
}

__attribute__((unused)) static inline void inst_barrier(void) {
    __asm volatile("isb 0xF" ::: "memory");
}

/* === 场景 B: 如果是 RISC-V 架构 (ESP32-C3 等) === */
#elif defined(__riscv)

// RISC-V 的屏障指令叫 fence

static inline void mem_barrier(void) {
    // RISC-V 的读写屏障
    __asm volatile("fence" ::: "memory");
}

static inline void sync_barrier(void) {
    // RISC-V 通常 fence 就够了，或者 fence.i 用于指令流
    __asm volatile("fence" ::: "memory");
}

static inline void inst_barrier(void) {
    // 刷新指令缓存 (Instruction Cache Flush)
    __asm volatile("fence.i" ::: "memory");
}

/* === 场景 C: 如果是 x86 (在电脑上跑模拟/单元测试) === */
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

/* === 场景 D: 兜底 (未知架构) === */
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