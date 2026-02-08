//
// Created by yan on 2025/10/18.
//

#ifndef RINGBUFFER_H
#define RINGBUFFER_H
#include <stdbool.h>

#include "ret_code.h"
#include "stdint.h"
#define DEFAULT_ALIGNMENT 4

typedef struct {
    const char *name;
    volatile uint32_t rear_index;   // 表示可以添加数据的头地址
    volatile uint32_t front_index;  // 表示可以被删除的头地址
    uint32_t size;                  // 缓冲区大小 实际可用-1
    uint8_t *buffer;                // 缓冲区头地址
    bool isPowerOfTwo_Size;
    volatile uint32_t high_watermark_used; /* 历史最大 used 字节数 */
    volatile uint32_t overflow_cnt;        /* 写入空间不足次数（非 force 或 reserve 失败等） */
    volatile uint32_t underflow_cnt;       /* 读取数据不足次数（非 force 等） */
    volatile uint32_t last_used;           /* 最近一次计算到的 used */
    volatile uint32_t last_remain;         /* 最近一次计算到的 remain */
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
    uint32_t contig_read;  /* 从 front 到尾部连续可读 */
    uint32_t contig_write; /* 从 rear  到尾部连续可写（含 wrap 约束） */

    /* 统计/水位线 */
    uint32_t high_watermark_used;
    uint32_t overflow_cnt;
    uint32_t underflow_cnt;
    uint32_t last_used;
    uint32_t last_remain;
} RingBufferStatus;

typedef struct {
    uint8_t *p1;
    uint32_t n1;
    uint8_t *p2;
    uint32_t n2;
} RingBufferSpan;
/**============================================================================================ */
/**==================================       BASE          ===================================== */
/**============================================================================================ */
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

ret_code_t ResetRingBuffer(RingBuffer *rb);

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
                                   uint32_t *granted, bool iSCompatible);

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
ret_code_t WriteRingBuffer_SPSC(RingBuffer *rb, const uint8_t *add, uint32_t *size,
                                uint8_t isForceWrite);
ret_code_t ReadRingBuffer_SPSC(RingBuffer *rb, uint8_t *add, uint32_t *size, uint8_t isForceRead);
ret_code_t PeekRingBuffer_SPSC(const RingBuffer *rb, uint8_t *add, uint32_t *size,
                               uint8_t isForcePeek);
ret_code_t RingBuffer_WriteReserve_SPSC(RingBuffer *rb, uint32_t want, RingBufferSpan *out,
                                        uint32_t *granted, bool isCompatible);
ret_code_t RingBuffer_WriteCommit_SPSC(RingBuffer *rb, uint32_t commit);
ret_code_t RingBuffer_ReadReserve_SPSC(RingBuffer *rb, uint32_t want, RingBufferSpan *out,
                                       uint32_t *granted, bool isCompatible);
ret_code_t RingBuffer_ReadCommit_SPSC(RingBuffer *rb, uint32_t commit);
/**============================================================================================ */
/**==================================       状态监测          =================================== */
/**============================================================================================ */

/* 轻量判定 */
bool RingBuffer_IsEmpty(const RingBuffer *rb);
bool RingBuffer_IsFull(const RingBuffer *rb);
uint16_t RingBuffer_GetUsedPermille(const RingBuffer *rb);
/* 阈值判定：用于“水位线事件” */
bool RingBuffer_UsedAtLeast(const RingBuffer *rb, uint32_t used_th);
bool RingBuffer_RemainAtMost(const RingBuffer *rb, uint32_t remain_th);
uint32_t RingBuffer_GetContigRead(const RingBuffer *rb);
uint32_t RingBuffer_GetContigWrite(const RingBuffer *rb);
/* 获取状态快照 */
RingBufferStatus RingBuffer_GetStatus(const RingBuffer *rb);
#endif  // RINGBUFFER_H
