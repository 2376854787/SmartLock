//
// Created by yan on 2025/10/18.
//

#ifndef RINGBUFFER_H
#define RINGBUFFER_H
#include <stdbool.h>

#include "stdint.h"
#define RING_BUFF_DEF_SIZE  1024 //单位字节
#define DEFAULT_ALIGNMENT 4

typedef struct {
    char *name;
    volatile uint32_t rear_index; //表示可以添加数据的头地址
    volatile uint32_t front_index; //表示可以被删除的头地址
    volatile uint32_t size; //缓冲区大小
    uint8_t *buffer; //缓冲区头地址
    bool isPowerOfTwo_Size;
} RingBuffer;

typedef struct {
    uint8_t *p1;
    uint32_t n1;
    uint8_t *p2;
    uint32_t n2;
} RingBufferSpan;


bool CreateRingBuffer(RingBuffer *rb, uint32_t size);

uint32_t RingBuffer_GetUsedSize(const RingBuffer *rb);

uint32_t RingBuffer_GetUsedSizeFromISR(const RingBuffer *rb);

uint32_t RingBuffer_GetRemainSize(const RingBuffer *rb);

uint32_t RingBuffer_GetRemainSizeFromISR(const RingBuffer *rb);

bool WriteRingBuffer(RingBuffer *rb, const uint8_t *add, uint32_t *size, uint8_t isForceWrite);

bool ReadRingBuffer(RingBuffer *rb, uint8_t *add, uint32_t *size, uint8_t isForceRead);

bool PeekRingBuffer(RingBuffer *rb, uint8_t *add, uint32_t *size, uint8_t isForcePeek);

bool WriteRingBufferFromISR(RingBuffer *rb, const uint8_t *add, uint32_t *size, uint8_t isForceWrite);

bool ReadRingBufferFromISR(RingBuffer *rb, uint8_t *add, uint32_t *size, uint8_t isForceRead);

bool ResetRingBuffer(RingBuffer *rb);

bool ResetRingBufferFromISR(RingBuffer *rb);

/*　零拷贝 写 */
bool RingBuffer_WriteReserve(RingBuffer *rb, uint32_t want, RingBufferSpan *out, uint32_t *granted, bool iSCompatible);

bool RingBuffer_WriteCommit(RingBuffer *rb, uint32_t commit);

bool RingBuffer_WriteReserveFromISR(RingBuffer *rb,
                                    uint32_t want,
                                    RingBufferSpan *out,
                                    uint32_t *granted,
                                    bool isCompatible);

bool RingBuffer_WriteCommitFromISR(RingBuffer *rb, uint32_t commit);

/* 零拷贝 读 */
bool RingBuffer_ReadReserve(RingBuffer *rb,
                            uint32_t want,
                            RingBufferSpan *out,
                            uint32_t *granted,
                            bool isCompatible);

bool RingBuffer_ReadReserveFromISR(RingBuffer *rb,
                                   uint32_t want,
                                   RingBufferSpan *out,
                                   uint32_t *granted,
                                   bool isCompatible);

bool RingBuffer_ReadCommit(RingBuffer *rb, uint32_t commit);

bool RingBuffer_ReadCommitFromISR(RingBuffer *rb, uint32_t commit);


/* 丢掉N字节 */
bool RingBuffer_Drop(RingBuffer *rb, uint32_t drop, uint32_t *dropped, bool isCompatible);

bool RingBuffer_DropFromISR(RingBuffer *rb, uint32_t drop, uint32_t *dropped, bool isCompatible);


#endif //RINGBUFFER_H
