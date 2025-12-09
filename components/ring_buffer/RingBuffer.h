//
// Created by yan on 2025/10/18.
//

#ifndef TEST_RINGBUFFER_H
#define TEST_RINGBUFFER_H
#include <stdbool.h>

#include "stdint.h"
#define RING_BUFF_DEF_SIZE  1024 //单位字节
#define DEFAULT_ALIGNMENT 4

typedef struct {
    char *name;
    volatile uint16_t rear_index; //表示可以添加数据的头地址
    volatile uint16_t front_index; //表示可以被删除的头地址
    volatile uint16_t size; //缓冲区大小
    uint8_t *buffer; //缓冲区头地址
    bool isPowerOfTwo_Size;
} RingBuffer;


bool CreateRingBuffer(RingBuffer *rb, uint16_t size);

uint16_t RingBuffer_GetUsedSize(const RingBuffer *rb);

uint16_t RingBuffer_GetRemainSize(const RingBuffer *rb);

bool WriteRingBuffer(RingBuffer *rb, const uint8_t *add, uint16_t *size, uint8_t isForceWrite);

bool ReadRingBuffer(RingBuffer *rb, uint8_t *add, uint16_t *size, uint8_t isForceRead);

bool PeekRingBuffer(RingBuffer *rb, uint8_t *add, uint16_t *size, const uint8_t isForcePeek);

bool WriteRingBufferFromISR(RingBuffer *rb, const uint8_t *add, uint16_t *size, const uint8_t isForceWrite);

bool ReadRingBufferFromISR(RingBuffer *rb, uint8_t *add, uint16_t *size, const uint8_t isForceRead) ;
#endif //TEST_RINGBUFFER_H
