#ifndef TEST_RINGBUFFER_H
#define TEST_RINGBUFFER_H

#include <stdbool.h>
#include <stdint.h>

#include "ret_code.h"

typedef struct {
    uint8_t* buffer;
    uint32_t size;
    uint32_t front_index;
    uint32_t rear_index;
} RingBuffer;

typedef struct {
    uint8_t* p1;
    uint32_t n1;
    uint8_t* p2;
    uint32_t n2;
} RingBufferSpan;

ret_code_t CreateRingBuffer(RingBuffer* rb, const char* name, uint32_t size);
ret_code_t ResetRingBuffer(RingBuffer* rb);
uint32_t RingBuffer_GetUsedSize(const RingBuffer* rb);
uint32_t RingBuffer_GetUsedSizeFromISR(const RingBuffer* rb);
ret_code_t RingBuffer_WriteReserve_SPSC(RingBuffer* rb, uint32_t want, RingBufferSpan* out,
                                        uint32_t* granted, bool isCompatible);
ret_code_t RingBuffer_WriteCommit_SPSC(RingBuffer* rb, uint32_t commit);
ret_code_t RingBuffer_ReadReserve_SPSC(RingBuffer* rb, uint32_t want, RingBufferSpan* out,
                                       uint32_t* granted, bool isCompatible);
ret_code_t RingBuffer_ReadCommit_SPSC(RingBuffer* rb, uint32_t commit);
void RingBuffer_SpanWriteFromLinear(const RingBufferSpan* span, const uint8_t* src, uint32_t len);
void RingBuffer_SpanReadToLinear(const RingBufferSpan* span, uint8_t* dst, uint32_t len);

#endif /* TEST_RINGBUFFER_H */
