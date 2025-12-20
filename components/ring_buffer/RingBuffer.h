//
// Created by yan on 2025/10/18.
//

#ifndef RINGBUFFER_H
#define RINGBUFFER_H
#include <stdbool.h>
#include  "ret_code.h"
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


ret_code_t CreateRingBuffer(RingBuffer *rb, uint32_t size);

uint32_t RingBuffer_GetUsedSize(const RingBuffer *rb);

uint32_t RingBuffer_GetUsedSizeFromISR(const RingBuffer *rb);

uint32_t RingBuffer_GetRemainSize(const RingBuffer *rb);

uint32_t RingBuffer_GetRemainSizeFromISR(const RingBuffer *rb);

ret_code_t WriteRingBuffer(RingBuffer *rb, const uint8_t *add, uint32_t *size, uint8_t isForceWrite);

ret_code_t ReadRingBuffer(RingBuffer *rb, uint8_t *add, uint32_t *size, uint8_t isForceRead);

ret_code_t PeekRingBuffer(RingBuffer *rb, uint8_t *add, uint32_t *size, uint8_t isForcePeek);

ret_code_t WriteRingBufferFromISR(RingBuffer *rb, const uint8_t *add, uint32_t *size, uint8_t isForceWrite);

ret_code_t ReadRingBufferFromISR(RingBuffer *rb, uint8_t *add, uint32_t *size, uint8_t isForceRead);

ret_code_t ResetRingBuffer(RingBuffer *rb);

ret_code_t ResetRingBufferFromISR(RingBuffer *rb);

/*　零拷贝 写 */
ret_code_t RingBuffer_WriteReserve(RingBuffer *rb, uint32_t want, RingBufferSpan *out, uint32_t *granted, bool iSCompatible);

ret_code_t RingBuffer_WriteCommit(RingBuffer *rb, uint32_t commit);

ret_code_t RingBuffer_WriteReserveFromISR(RingBuffer *rb,
                                    uint32_t want,
                                    RingBufferSpan *out,
                                    uint32_t *granted,
                                    bool isCompatible);

ret_code_t RingBuffer_WriteCommitFromISR(RingBuffer *rb, uint32_t commit);

/* 零拷贝 读 */
ret_code_t RingBuffer_ReadReserve(RingBuffer *rb,
                            uint32_t want,
                            RingBufferSpan *out,
                            uint32_t *granted,
                            bool isCompatible);

ret_code_t RingBuffer_ReadReserveFromISR(RingBuffer *rb,
                                   uint32_t want,
                                   RingBufferSpan *out,
                                   uint32_t *granted,
                                   bool isCompatible);

ret_code_t RingBuffer_ReadCommit(RingBuffer *rb, uint32_t commit);

ret_code_t RingBuffer_ReadCommitFromISR(RingBuffer *rb, uint32_t commit);


/* 丢掉N字节 */
ret_code_t RingBuffer_Drop(RingBuffer *rb, uint32_t drop, uint32_t *dropped, bool isCompatible);

ret_code_t RingBuffer_DropFromISR(RingBuffer *rb, uint32_t drop, uint32_t *dropped, bool isCompatible);


#endif //RINGBUFFER_H
