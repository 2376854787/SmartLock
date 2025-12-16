//
// Created by yan on 2025/10/18.
//
#define BufferMaxSize 8192
#include "RingBuffer.h"

#include <stdbool.h>
#include <string.h>
#include "FreeRTOS.h"
#include "log.h"
#include "MemoryAllocation.h"
#include "task.h"


/* 任务中进入临界区的宏 */
#define RB_ENTER_CRITICAL()   taskENTER_CRITICAL()
#define RB_EXIT_CRITICAL()    taskEXIT_CRITICAL()

/* 中断中可以使用的临界区 */
#define RB_ENTER_CRITICAL_FROM_ISR(saved)   do { (saved) = taskENTER_CRITICAL_FROM_ISR(); } while (0)
#define RB_EXIT_CRITICAL_FROM_ISR(saved)    do { taskEXIT_CRITICAL_FROM_ISR((saved)); } while (0)


static inline uint32_t RingBuffer_GetUsedSize_Internal(const RingBuffer *rb);

static inline uint32_t RingBuffer_GetRemainSize_Internal(const RingBuffer *rb);

/**
 * @brief  创建一个指定大小的环形缓冲区
 * @param rb 环形缓冲区句柄
 * @param size 要分配的缓冲区大小 （有效空间 size-1）
 * @return 返回是否创建成功
 */
bool CreateRingBuffer(RingBuffer *rb, const uint32_t size) {
    //1、检擦输入参数的合法性
    if (rb == NULL || size < 2)return false;

    //2、动态分配内存
    rb->buffer = static_alloc(size, DEFAULT_ALIGNMENT);

    //3、检查分配是否成功
    if (rb->buffer == NULL) {
        rb->front_index = rb->rear_index = 0;
        rb->size = 0;
        rb->isPowerOfTwo_Size = false;
        return false;
    }

    //4、分配成功
    rb->front_index = 0;
    rb->rear_index = 0;
    rb->size = size;
    rb->isPowerOfTwo_Size = (rb->size != 0) && ((rb->size & (rb->size - 1)) == 0);
    // 简洁且安全地判断是否是2的幂; //判断缓冲区大小是不是2得幂 用于高效判断
    return true;
}

/**
 * @brief  获取环形缓冲区中已存储的数据量
 * @param  rb 指向 RingBuffer 结构体的指针
 * @return 返回已存储的数据量 (字节数)。如果 rb 为 NULL，返回 0。
 */
uint32_t RingBuffer_GetUsedSize(const RingBuffer *rb) {
    if (rb == NULL || rb->size < 2 || rb->buffer == NULL) return 0;
    //进入临界区
    RB_ENTER_CRITICAL();
    const uint32_t used_size = RingBuffer_GetUsedSize_Internal(rb);
    //退出临界区
    RB_EXIT_CRITICAL();
    return used_size;
}

/**
 * @brief  获取环形缓冲区中已存储的数据量
 * @param  rb 指向 RingBuffer 结构体的指针
 * @return 返回已存储的数据量 (字节数)。如果 rb 为 NULL，返回 0。
 */
uint32_t RingBuffer_GetUsedSizeFromISR(const RingBuffer *rb) {
    //进入临界区
    if (rb == NULL || rb->size < 2 || rb->buffer == NULL) return 0;
    UBaseType_t saved;
    RB_ENTER_CRITICAL_FROM_ISR(saved);
    const uint32_t used_size = RingBuffer_GetUsedSize_Internal(rb);
    //退出临界区
    RB_EXIT_CRITICAL_FROM_ISR(saved);

    return used_size;
}

/**
 * @brief  获取环形缓冲区中已存储的数据量
 * @param  rb 指向 RingBuffer 结构体的指针
 * @return 返回已存储的数据量 (字节数)。如果 rb 为 NULL，返回 0。
 */
static inline uint32_t RingBuffer_GetUsedSize_Internal(const RingBuffer *rb) {
    if (rb == NULL || rb->size < 2) return 0;

    const uint32_t rear = rb->rear_index;

    const uint32_t front = rb->front_index;

    const uint32_t size = rb->size;
    uint32_t used_size;
    if (rb->isPowerOfTwo_Size) {
        used_size = (rear - front + size) & (size - 1);
    } else {
        used_size = (rear - front + size) % size;
    }

    return used_size;
}


/**
 * @brief  获取环形缓冲区中可使用的数据量
 * @param  rb 指向 RingBuffer 结构体的指针
 * @return 返回可使用的数据量 (字节数)。如果 rb 为 NULL，返回 0。
 */
uint32_t RingBuffer_GetRemainSize(const RingBuffer *rb) {
    if (rb == NULL || rb->size < 2 || rb->buffer == NULL) return 0;
    //进入临界区
    RB_ENTER_CRITICAL();
    const uint32_t size = RingBuffer_GetRemainSize_Internal(rb);
    //退出临界区
    RB_EXIT_CRITICAL();
    // 未可使用空间
    return size;
}

/**
 * @brief  获取环形缓冲区中可使用的数据量
 * @param  rb 指向 RingBuffer 结构体的指针
 * @return 返回可使用的数据量 (字节数)。如果 rb 为 NULL，返回 0。
 */
uint32_t RingBuffer_GetRemainSizeFromISR(const RingBuffer *rb) {
    if (rb == NULL || rb->size < 2 || rb->buffer == NULL) return 0;
    //进入临界区
    UBaseType_t saved;
    RB_ENTER_CRITICAL_FROM_ISR(saved);
    const uint32_t size = RingBuffer_GetRemainSize_Internal(rb);
    //退出临界区
    RB_EXIT_CRITICAL_FROM_ISR(saved);
    // 未可使用空间
    return size;
}

/**
 * @brief  获取环形缓冲区中可使用的数据量
 * @param  rb 指向 RingBuffer 结构体的指针
 * @return 返回可使用的数据量 (字节数)。如果 rb 为 NULL，返回 0。
 */
static inline uint32_t RingBuffer_GetRemainSize_Internal(const RingBuffer *rb) {
    if (rb == NULL || rb->size < 2) return 0;
    return rb->size - RingBuffer_GetUsedSize_Internal(rb) - 1;
}


/**
 * @brief  添加数据到环形缓冲区
 * @param rb 环形缓冲区指针
 * @param add 数据源地址
 * @param size 添加的数据大小（字节）
 * @param isForceWrite 是否强制写入可写入得长度数据剩余丢弃
 * @return 返回是否成功
 */
bool WriteRingBuffer(RingBuffer *rb, const uint8_t *add, uint32_t *size, const uint8_t isForceWrite) {
    //1、参数合法性检查
    if (rb == NULL || add == NULL || *size == 0 || rb->buffer == NULL || rb->size < 2)return false;
    // --- 进入临界区，保护所有对 rb 成员的访问 ---
    RB_ENTER_CRITICAL();
    //2、检查当前缓冲区的大小是否能够装入
    const uint32_t remain_size = RingBuffer_GetRemainSize_Internal(rb);
    if (remain_size < *size) {
        if (isForceWrite) {
            *size = remain_size;
        } else {
            // --- 在返回前，退出临界区 ---
            RB_EXIT_CRITICAL();
            return false;
        }
    }

    // 如果窥视大小为0（可能在isForcePeek后发生），则直接成功返回
    if (*size == 0) {
        RB_EXIT_CRITICAL();
        return true;
    }

    //3、写入缓冲区
    //判断当前的空间是否足够一次性写入
    const uint32_t end_size = rb->size - rb->rear_index;
    if (end_size >= *size) {
        memcpy(rb->buffer + rb->rear_index, add, *size);
    } else {
        //分两段分别写入
        memcpy(rb->buffer + rb->rear_index, add, end_size);
        memcpy(rb->buffer, add + end_size, *size - end_size);
    }
    //幂运算优化取余运算
    if (rb->isPowerOfTwo_Size) {
        rb->rear_index = (rb->rear_index + *size) & (rb->size - 1);
    } else {
        rb->rear_index = (rb->rear_index + *size) % rb->size;
    }

    // --- 在返回前，退出临界区 ---
    RB_EXIT_CRITICAL();
    return true;
}

/**
 *
 * @param rb 环形缓冲区指针
 * @param add 接收数据的地址
 * @param size 要获取的数据大小（字节
 * @param isForceRead 是否在数据不足 @param size 大小时候强制读取已有的全部数据
 * @return 返回是否读取成功
 */
bool ReadRingBuffer(RingBuffer *rb, uint8_t *add, uint32_t *size, const uint8_t isForceRead) {
    //1、参数合法性检查
    if (rb == NULL || add == NULL || *size == 0 || rb->buffer == NULL || rb->size < 2)return false;
    // --- 进入临界区，保护所有对 rb 成员的访问 ---
    RB_ENTER_CRITICAL();
    const uint32_t usedSize = RingBuffer_GetUsedSize_Internal(rb);
    //2、检查当前缓冲区的大小是否能够装入
    if (usedSize < *size) {
        //强制读取剩余的
        if (isForceRead) {
            *size = usedSize;
        } else {
            // --- 在返回前，退出临界区 ---
            RB_EXIT_CRITICAL();
            //不强制读取当前数据不足就不读取
            return false;
        }
    }

    // 如果窥视大小为0（可能在isForcePeek后发生），则直接成功返回
    if (*size == 0) {
        RB_EXIT_CRITICAL();
        return true;
    }

    //3、写入缓冲区
    //判断当前的空间是否足够一次性读取
    const uint32_t end_size = rb->size - rb->front_index;
    if (end_size >= *size) {
        memcpy(add, rb->buffer + rb->front_index, *size);
    } else {
        //分两段分别读取写入
        memcpy(add, rb->buffer + rb->front_index, end_size);
        memcpy(add + end_size, rb->buffer, *size - end_size);
    }
    //幂运算优化取余运算
    if (rb->isPowerOfTwo_Size) {
        rb->front_index = (rb->front_index + *size) & (rb->size - 1);
    } else {
        rb->front_index = (rb->front_index + *size) % rb->size;
    }

    // --- 在返回前，退出临界区 ---
    RB_EXIT_CRITICAL();
    return true;
}

/**
 * @brief  窥视环形缓冲区中的数据，但不移动读指针
 * @param  rb          环形缓冲区指针
 * @param  add         接收数据的地址
 * @param  size        要窥视的数据大小（字节）
 * @param  isForcePeek 是否在数据不足时强制窥视已有的数据
 * @return 返回是否成功窥视（如果请求的数据量大于已用空间且非强制，则失败）
 */
bool PeekRingBuffer(RingBuffer *rb, uint8_t *add, uint32_t *size, const uint8_t isForcePeek) {
    // 1、参数合法性检查
    if (rb == NULL || add == NULL || *size == 0 || rb->buffer == NULL || rb->size < 2)return false;

    // --- 进入临界区，保护所有对 rb 成员的访问 ---
    RB_ENTER_CRITICAL(); // 注意：如果是在任务中调用，应使用 taskENTER_CRITICAL()

    const uint32_t usedSize = RingBuffer_GetUsedSize_Internal(rb);

    // 2、检查当前缓冲区中是否有足够的数据
    if (usedSize < *size) {
        // 强制窥视剩余的
        if (isForcePeek) {
            *size = usedSize;
        } else {
            // --- 在返回前，退出临界区 ---
            RB_EXIT_CRITICAL();
            // 不强制窥视，且数据不足，则操作失败
            return false;
        }
    }

    // 如果窥视大小为0（可能在isForcePeek后发生），则直接成功返回
    if (*size == 0) {
        RB_EXIT_CRITICAL();
        return true;
    }

    // 3、从缓冲区复制数据
    // 判断当前的空间是否足够一次性复制
    const uint32_t end_size = rb->size - rb->front_index;
    if (end_size >= *size) {
        memcpy(add, rb->buffer + rb->front_index, *size);
    } else {
        // 分两段分别复制
        memcpy(add, rb->buffer + rb->front_index, end_size);
        memcpy(add + end_size, rb->buffer, *size - end_size);
    }

    // --- 在返回前，退出临界区 ---
    RB_EXIT_CRITICAL();
    return true;
}

/**
 * @brief  添加数据到环形缓冲区 中断版本
 * @param rb 环形缓冲区指针
 * @param add 数据源地址
 * @param size 添加的数据大小（字节）
 * @param isForceWrite 是否强制写入可写入得长度数据剩余丢弃
 * @return 返回是否成功
 */
bool WriteRingBufferFromISR(RingBuffer *rb, const uint8_t *add, uint32_t *size, const uint8_t isForceWrite) {
    //1、参数合法性检查
    if (rb == NULL || add == NULL || *size == 0 || rb->buffer == NULL || rb->size < 2)return false;
    // --- 进入临界区，保护所有对 rb 成员的访问 ---
    UBaseType_t saved;
    RB_ENTER_CRITICAL_FROM_ISR(saved);
    //2、检查当前缓冲区的大小是否能够装入
    const uint32_t remain_size = RingBuffer_GetRemainSize_Internal(rb);
    if (remain_size < *size) {
        if (isForceWrite) {
            *size = remain_size;
        } else {
            // --- 在返回前，退出临界区 ---
            RB_EXIT_CRITICAL_FROM_ISR(saved);
            return false;
        }
    }

    // 如果窥视大小为0（可能在isForcePeek后发生），则直接成功返回
    if (*size == 0) {
        RB_EXIT_CRITICAL_FROM_ISR(saved);
        return true;
    }

    //3、写入缓冲区
    //判断当前的空间是否足够一次性写入
    const uint32_t end_size = rb->size - rb->rear_index;
    if (end_size >= *size) {
        memcpy(rb->buffer + rb->rear_index, add, *size);
    } else {
        //分两段分别写入
        memcpy(rb->buffer + rb->rear_index, add, end_size);
        memcpy(rb->buffer, add + end_size, *size - end_size);
    }
    //幂运算优化取余运算
    if (rb->isPowerOfTwo_Size) {
        rb->rear_index = (rb->rear_index + *size) & (rb->size - 1);
    } else {
        rb->rear_index = (rb->rear_index + *size) % rb->size;
    }

    // --- 在返回前，退出临界区 ---
    RB_EXIT_CRITICAL_FROM_ISR(saved);
    return true;
}

/**
 *
 * @param rb 环形缓冲区指针中断版本
 * @param add 接收数据的地址
 * @param size 要获取的数据大小（字节
 * @param isForceRead 是否在数据不足 @param size 大小时候强制读取已有的全部数据
 * @return 返回是否读取成功
 */
bool ReadRingBufferFromISR(RingBuffer *rb, uint8_t *add, uint32_t *size, const uint8_t isForceRead) {
    //1、参数合法性检查
    if (rb == NULL || add == NULL || *size == 0 || rb->buffer == NULL || rb->size < 2)return false;
    // --- 进入临界区，保护所有对 rb 成员的访问 ---
    UBaseType_t saved;
    RB_ENTER_CRITICAL_FROM_ISR(saved);
    const uint32_t usedSize = RingBuffer_GetUsedSize_Internal(rb);
    //2、检查当前缓冲区的大小是否能够装入
    if (usedSize < *size) {
        //强制读取剩余的
        if (isForceRead) {
            *size = usedSize;
        } else {
            // --- 在返回前，退出临界区 ---
            RB_EXIT_CRITICAL_FROM_ISR(saved);
            //不强制读取当前数据不足就不读取
            return false;
        }
    }

    // 如果窥视大小为0（可能在isForcePeek后发生），则直接成功返回
    if (*size == 0) {
        RB_EXIT_CRITICAL_FROM_ISR(saved);
        return true;
    }

    //3、写入缓冲区
    //判断当前的空间是否足够一次性读取
    const uint32_t end_size = rb->size - rb->front_index;
    if (end_size >= *size) {
        memcpy(add, rb->buffer + rb->front_index, *size);
    } else {
        //分两段分别读取写入
        memcpy(add, rb->buffer + rb->front_index, end_size);
        memcpy(add + end_size, rb->buffer, *size - end_size);
    }
    //幂运算优化取余运算
    if (rb->isPowerOfTwo_Size) {
        rb->front_index = (rb->front_index + *size) & (rb->size - 1);
    } else {
        rb->front_index = (rb->front_index + *size) % rb->size;
    }

    // --- 在返回前，退出临界区 ---
    RB_EXIT_CRITICAL_FROM_ISR(saved);
    return true;
}

/**
 * @brief 重置缓冲区
 * @param rb 句柄
 * @return
 */
bool ResetRingBuffer(RingBuffer *rb) {
    if (rb == NULL) return false;
    RB_ENTER_CRITICAL();
    rb->front_index = 0;
    rb->rear_index = 0;
    RB_EXIT_CRITICAL();
    return true;
}

/**
 * @brief 重置缓冲区 中断版本
 * @param rb 句柄
 * @return
 */
bool ResetRingBufferFromISR(RingBuffer *rb) {
    if (rb == NULL) {
        return false;
    }
    UBaseType_t saved;
    RB_ENTER_CRITICAL_FROM_ISR(saved);
    rb->front_index = 0;
    rb->rear_index = 0;
    RB_EXIT_CRITICAL_FROM_ISR(saved);
    return true;
}
