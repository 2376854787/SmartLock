//
// Created by yan on 2025/10/18.
//

#ifndef RINGBUFFER_H
#define RINGBUFFER_H
#include <stdbool.h>

#include "ret_code_t.h"
#include "stdint.h"
#define DEFAULT_ALIGNMENT 4

/* 运行时统计开关（水位线 / overflow / underflow / last_used / last_remain）。
 * 默认开启。关闭（编译时定义 RB_ENABLE_STATS=0）后：
 *   - RingBuffer / RingBufferStatus 不再保留这些字段，省 RAM；
 *   - 每次 Push/Pop/Commit 的热路径不再做水位线计算，少一次 used 重算和几次
 *     volatile 写——对标 lwrb 的零开销热路径。
 * 统计只是可观测性，关掉不影响队列功能。 */
#ifndef RB_ENABLE_STATS
#define RB_ENABLE_STATS 1
#endif

/* ============================================================================
 * 并发模型（重要）：同一个 RingBuffer 实例只能选用以下一种模型，禁止混用：
 *
 *   A. 带锁模型：使用不带 _SPSC 后缀的接口（WriteRingBuffer / ReadRingBuffer /
 *      *FromISR / WriteReserve+Commit 等），所有接口内部用临界区互斥。
 *
 *   B. SPSC 无锁模型：使用带 _SPSC 后缀的接口，必须是“单生产者 + 单消费者”，
 *      生产者只写、消费者只读，内部用内存屏障代替临界区。
 *
 *   C. 零拷贝单方 Reserve/Commit：Reserve 返回的窗口在 Commit 之前由调用方
 *      独占，同一侧不能并发。
 *
 * 混用 A 和 B（例如 ISR 用 SPSC 写、线程用带锁读）会数据竞争，因为 SPSC 侧
 * 无锁，看不到带锁侧临界区内的中间状态。
 *
 * 统计字段（high_watermark_used 等）在 SPSC 模型下仅由生产者侧更新，避免与
 * 消费者侧的 RMW 冲突。详见 RingBuffer.c 顶部注释。
 * ========================================================================= */

/* SPSC 路径的「对端类型」——决定发布/同步用哪种内存屏障 */

typedef enum { RB_SYNC_SMP = 1, RB_SYNC_DMA = 2 } rb_sync_t;

typedef struct {
    const char *name;
    /* rear_index/front_index 是「自由递增」的写/读字节序号（和 DPDK rte_ring 的
     * head/tail 同构）：它们不在 [0,size) 内回绕，而是用 uint32 一直增长、自然
     * 绕过 UINT32_MAX。定位物理槽时用 index & mask，已用字节数用无符号减法
     * rear-front 直接得到（即使 rear 绕过 0 也正确）。
     *
     * 为什么必须 2 的幂：自由递增方案只在 size 是 2 的幂时正确。rear/front 在
     * UINT32_MAX 处回绕，只有当 size 是 2 的幂、mask=size-1 时，(index & mask)
     * 才能在回绕点（UINT32_MAX -> 0）保持物理槽连续；非 2 的幂会在回绕处错位。
     * CreateRingBuffer 强制把请求的 size 向上取整到 2 的幂来保证这一点。 */
    volatile uint32_t rear_index;   // 写序号（自由递增），物理槽 = rear_index & mask
    volatile uint32_t front_index;  // 读序号（自由递增），物理槽 = front_index & mask
    uint32_t size;                  // 缓冲区字节数（2 的幂），即满容量（无哨兵，可全用）
    uint8_t *buffer;                // 缓冲区头地址
    uint32_t mask;                  // = size - 1，用于 & mask 定位物理槽
#if RB_ENABLE_STATS
    volatile uint32_t high_watermark_used; /* 历史最大 used 字节数 */
    volatile uint32_t overflow_cnt;        /* 写入空间不足次数（非 force 或 reserve 失败等） */
    volatile uint32_t underflow_cnt;       /* 读取数据不足次数（非 force 等） */
    volatile uint32_t last_used;           /* 最近一次计算到的 used */
    volatile uint32_t last_remain;         /* 最近一次计算到的 remain */
#endif
} RingBuffer;
/* RB 运行时快照 */
typedef struct {
    uint32_t used;
    uint32_t remain;
    uint32_t size;          /* rb->size */
    uint16_t used_permille; /* 0~1000 */
    bool empty;
    bool full;

    /* 连续段：适配 DMA / 零拷贝调度 */
    uint32_t contig_read;  /* 从 front 物理槽到尾部连续可读 */
    uint32_t contig_write; /* 从 rear  物理槽到尾部连续可写（受 remain 约束，无哨兵） */

#if RB_ENABLE_STATS
    /* 统计/水位线 */
    uint32_t high_watermark_used;
    uint32_t overflow_cnt;
    uint32_t underflow_cnt;
    uint32_t last_used;
    uint32_t last_remain;
#endif
} RingBufferStatus;

typedef struct {
    uint8_t *p1;
    uint32_t n1;
    uint8_t *p2;
    uint32_t n2;
} RingBufferSpan;

/**============================================================================================ */
/**==================================      Span拷贝工具     ===================================== */
/**============================================================================================ */

void RingBuffer_SpanWriteFromLinear(const RingBufferSpan *span, const uint8_t *src, uint32_t len);

void RingBuffer_SpanReadToLinear(const RingBufferSpan *span, uint8_t *dst, uint32_t len);

void RingBuffer_SpanWriteFromCircular(const RingBufferSpan *span, const uint8_t *src_ring,
                                      uint32_t src_ring_len, uint32_t src_pos, uint32_t len);
/**============================================================================================ */
/**==================================       BASE          ===================================== */
/**============================================================================================ */
/* 注意：实际分配的缓冲区是把 size 向上取整到 2 的幂后的大小（如请求 1000 → 1024），
 * 取整后的容量可全部使用（无哨兵）。用 rb->size 读回实际容量。 */
ret_code_t CreateRingBuffer(RingBuffer *rb, const char *name, uint32_t size);

uint32_t RingBuffer_GetUsedSize(const RingBuffer *rb);

uint32_t RingBuffer_GetUsedSizeFromISR(const RingBuffer *rb);

uint32_t RingBuffer_GetRemainSize(const RingBuffer *rb);

uint32_t RingBuffer_GetRemainSizeFromISR(const RingBuffer *rb);

ret_code_t WriteRingBuffer(RingBuffer *rb, const uint8_t *add, uint32_t *size,
                           uint8_t isForceWrite);

ret_code_t ReadRingBuffer(RingBuffer *rb, uint8_t *add, uint32_t *size, uint8_t isForceRead);

ret_code_t PeekRingBuffer(const RingBuffer *rb, uint8_t *add, uint32_t *size, uint8_t isForcePeek);

ret_code_t WriteRingBufferFromISR(RingBuffer *rb, const uint8_t *add, uint32_t *size,
                                  uint8_t isForceWrite);

ret_code_t ReadRingBufferFromISR(RingBuffer *rb, uint8_t *add, uint32_t *size, uint8_t isForceRead);

/* 全量复位（front/rear 同时清零）。仅当生产者与消费者都已停止时可调——
 * 它是两个索引的"第二写者"，SPSC 下临界区挡不住无锁对端/另一核/DMA。
 * 运行期丢弃积压数据请用 RingBuffer_ResetByConsumer()。 */
ret_code_t ResetRingBuffer(RingBuffer *rb);

/* 消费者侧安全清空（front 追平 rear，对标 kfifo_reset_out）：只写消费者自己的
 * 索引，生产者/DMA 运行中也可安全调用。仅限消费者上下文。 */
ret_code_t RingBuffer_ResetByConsumer(RingBuffer *rb);

/* 丢掉N字节 */
ret_code_t RingBuffer_Drop(RingBuffer *rb, uint32_t drop, uint32_t *dropped, bool isCompatible);

ret_code_t RingBuffer_DropFromISR(RingBuffer *rb, uint32_t drop, uint32_t *dropped,
                                  bool isCompatible);

ret_code_t ResetRingBufferFromISR(RingBuffer *rb);
/**============================================================================================ */
/**==================================       零拷贝          ==================================== */
/**============================================================================================ */
/*　零拷贝 写 */
ret_code_t RingBuffer_WriteReserve(RingBuffer *rb, uint32_t want, RingBufferSpan *out,
                                   uint32_t *granted, bool isCompatible);

ret_code_t RingBuffer_WriteCommit(RingBuffer *rb, uint32_t commit);

ret_code_t RingBuffer_WriteReserveFromISR(RingBuffer *rb, uint32_t want, RingBufferSpan *out,
                                          uint32_t *granted, bool isCompatible);

ret_code_t RingBuffer_WriteCommitFromISR(RingBuffer *rb, uint32_t commit);

/* 零拷贝 读 */
ret_code_t RingBuffer_ReadReserve(RingBuffer *rb, uint32_t want, RingBufferSpan *out,
                                  uint32_t *granted, bool isCompatible);

ret_code_t RingBuffer_ReadReserveFromISR(RingBuffer *rb, uint32_t want, RingBufferSpan *out,
                                         uint32_t *granted, bool isCompatible);

ret_code_t RingBuffer_ReadCommit(RingBuffer *rb, uint32_t commit);

ret_code_t RingBuffer_ReadCommitFromISR(RingBuffer *rb, uint32_t commit);

/**============================================================================================ */
/**==================================       SPSC          ===================================== */
/**============================================================================================ */
/* 所有 SPSC 接口都把「对端类型」作为显式 sync 参数（rb_sync_t）：
 *   - 对端是软件线程/ISR  → 传 RB_SYNC_SMP（最常见）
 *   - 对端是 DMA/外设     → 传 RB_SYNC_DMA（零拷贝 Reserve/Commit 配合 DMA 时）
 * 同一个 RB 的生产者侧与消费者侧各自按自己的对端如实填，不要求两侧一致。 */
ret_code_t WriteRingBuffer_SPSC(RingBuffer *rb, const uint8_t *add, uint32_t *size,
                                uint8_t isForceWrite, rb_sync_t sync);
ret_code_t ReadRingBuffer_SPSC(RingBuffer *rb, uint8_t *add, uint32_t *size, uint8_t isForceRead,
                               rb_sync_t sync);
ret_code_t PeekRingBuffer_SPSC(const RingBuffer *rb, uint8_t *add, uint32_t *size,
                               uint8_t isForcePeek, rb_sync_t sync);
ret_code_t RingBuffer_WriteReserve_SPSC(RingBuffer *rb, uint32_t want, RingBufferSpan *out,
                                        uint32_t *granted, bool isCompatible, rb_sync_t sync);
ret_code_t RingBuffer_WriteCommit_SPSC(RingBuffer *rb, uint32_t commit, rb_sync_t sync);
ret_code_t RingBuffer_ReadReserve_SPSC(RingBuffer *rb, uint32_t want, RingBufferSpan *out,
                                       uint32_t *granted, bool isCompatible, rb_sync_t sync);
ret_code_t RingBuffer_ReadCommit_SPSC(RingBuffer *rb, uint32_t commit, rb_sync_t sync);
/**============================================================================================ */
/**==================================       状态监测          =================================== */
/**============================================================================================ */

/* 轻量判定 */
bool RingBuffer_IsEmpty(const RingBuffer *rb);
bool RingBuffer_IsFull(const RingBuffer *rb);
uint16_t RingBuffer_GetUsedPermille(const RingBuffer *rb);
uint16_t RingBuffer_GetUsedPermilleFromISR(const RingBuffer *rb);
/* 阈值判定：用于“水位线事件” */
bool RingBuffer_UsedAtLeast(const RingBuffer *rb, uint32_t used_th);
bool RingBuffer_RemainAtMost(const RingBuffer *rb, uint32_t remain_th);
uint32_t RingBuffer_GetContigRead(const RingBuffer *rb);
uint32_t RingBuffer_GetContigWrite(const RingBuffer *rb);
/* 获取状态快照 */
RingBufferStatus RingBuffer_GetStatus(const RingBuffer *rb);

#endif  // RINGBUFFER_H
