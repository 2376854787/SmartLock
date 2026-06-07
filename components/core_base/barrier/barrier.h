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
 *   - RB_SMP==0（默认，单核）：编译成 compiler_barrier()，展开后 0 条 CPU 指令
 *   - RB_SMP==1（多核）      ：编译成架构对应的轻量内存屏障
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

/* -----------------------------------------------------------------------------
 * DMA / 外设专用屏障：dma_mem_barrier()
 *
 * 和 smp_mem_barrier() 的关键区别：smp_ 版本在单核（RB_SMP==0）退化为纯编译器
 * 屏障，因为单核上「软件↔软件」两端共享同一个核的访存顺序，硬件天然保序。但
 * 当 SPSC 队列的「对端」不是另一个软件执行流、而是 DMA 控制器或外设时，这个前提
 * 不成立：
 *   - Cortex-M7 等核有 store buffer / 写合并，CPU 写到 SRAM 的「全局可见顺序」
 *     不保证等于程序序；DMA 主控经总线看到的顺序可能与 CPU 期望不一致。
 *   - 于是即使单核，「先写数据、再发布索引」也必须用真正的 DMB 强制顺序，
 *     否则 DMA 可能搬走一个索引已更新但数据还没落地的槽。
 *
 * 因此 dma_mem_barrier() 不看 RB_SMP，只要架构存在 DMA/外设这类内存观察者就发
 * 真正的内存屏障指令（ARM 用 full-system DMB，覆盖 device + normal memory）。
 * 用于「环的一端是 DMA」的 SPSC 路径（见 RingBuffer 的 *_SPSC_DMA 接口）。
 * --------------------------------------------------------------------------- */
#if defined(__arm__) || defined(__aarch64__)
__attribute__((unused)) static inline void dma_mem_barrier(void) {
    /* full-system DMB：同时覆盖 device 内存（外设寄存器）与 normal 内存，
     * 保证 CPU 对缓冲区的写在索引发布前对 DMA 可见 */
    __asm volatile("dmb 0xF" ::: "memory");
}
#elif defined(__riscv)
__attribute__((unused)) static inline void dma_mem_barrier(void) {
    /* 覆盖外设 I/O 与内存的完整 fence */
    __asm volatile("fence iorw, iorw" ::: "memory");
}
#elif defined(__x86_64__) || defined(__i386__)
__attribute__((unused)) static inline void dma_mem_barrier(void) {
    __asm volatile("mfence" ::: "memory");
}
#else
__attribute__((unused)) static inline void dma_mem_barrier(void) {
    /* 未知架构兜底：发最强屏障 */
    sync_barrier();
}
#endif

#endif  // ARCH_BARRIER_H