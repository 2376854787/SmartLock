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

/* -----------------------------------------------------------------------------
 * SPSC（单生产者单消费者）专用屏障：smp_mem_barrier()
 *
 * 为什么单独搞一个，而不是直接用 mem_barrier()：
 *   mem_barrier() 是 full-system DMB（dmb 0xF / SY），用于和「另一个观察者」
 *   同步——外设、DMA、跨核。代价最重。而 SPSC 队列的生产者/消费者只是两个
 *   线程/中断，它们之间的可见性需求取决于这两端有没有真的跑在不同的核上：
 *     - 单核（Cortex-M 常态）：同一个核先后执行，硬件天然保序，根本不需要
 *       任何 CPU 屏障指令，只要拦住「编译器」乱序即可。此时 DMB 是纯浪费，
 *       而 SPSC 卖点就是「无锁更快」，被一条 DMB SY 吃掉就本末倒置了。
 *     - 多核（如 M7+M4 双核）：两端可能真并行，才需要一条屏障保证跨核可见，
 *       且用 inner-shareable（ISH）即可，比 SY 轻。
 *
 * 所以这里按 RB_SMP 在「编译期」二选一，零运行时分支、零函数指针：
 *   - RB_SMP==0（默认，单核）：编译成 compiler_barrier()，展开后 0 条 CPU 指令
 *   - RB_SMP==1（多核）      ：编译成架构对应的轻量内存屏障
 *
 * 将来上双核，只需在编译选项里 -DRB_SMP=1，源码一行不用动。
 * --------------------------------------------------------------------------- */
#ifndef RB_SMP
#define RB_SMP 0 /* 默认单核：SPSC 屏障退化为编译器屏障，零指令 */
#endif

#if (RB_SMP == 0)

/* 单核：只拦编译器乱序，不发任何 CPU 屏障指令 */
__attribute__((unused)) static inline void smp_mem_barrier(void) {
    __asm volatile("" ::: "memory");
}

#else /* RB_SMP != 0：多核，需要真正的跨核内存屏障 */

#if defined(__arm__) || defined(__aarch64__)
__attribute__((unused)) static inline void smp_mem_barrier(void) {
    /* inner-shareable DMB：覆盖同一内部共享域内的其它核，比 full-system 轻 */
    __asm volatile("dmb ish" ::: "memory");
}
#elif defined(__riscv)
__attribute__((unused)) static inline void smp_mem_barrier(void) {
    __asm volatile("fence rw, rw" ::: "memory");
}
#elif defined(__x86_64__) || defined(__i386__)
__attribute__((unused)) static inline void smp_mem_barrier(void) {
    /* x86 是强序模型，普通读写之间只需拦编译器，不必发 mfence */
    __asm volatile("" ::: "memory");
}
#else
__attribute__((unused)) static inline void smp_mem_barrier(void) {
    /* 未知架构兜底：退回 full-system 屏障，保守但安全 */
    mem_barrier();
}
#endif

#endif /* RB_SMP */

#endif  // ARCH_BARRIER_H