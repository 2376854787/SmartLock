#include "APP_config.h"
/* 全局配置开启宏 */
#ifdef ENABLE_RINGBUFFER_SYSTEM
#include "RingBuffer.h"

#include <stdbool.h>
#include <string.h>
#include "MemoryAllocation.h"
#include "rb_port.h"

static inline uint32_t RingBuffer_GetUsedSize_Internal(const RingBuffer *rb);

static inline uint32_t RingBuffer_GetRemainSize_Internal(const RingBuffer *rb);

/**
 * @brief  创建一个指定大小的环形缓冲区
 * @param rb 环形缓冲区句柄
 * @param size 要分配的缓冲区大小 （有效空间 size-1）
 * @return 返回是否创建成功
 */
ret_code_t CreateRingBuffer(RingBuffer *rb, const uint32_t size) {
    //1、检擦输入参数的合法性
    if (rb == NULL || size < 2)return RET_E_INVALID_ARG;

    //2、动态分配内存
    rb->buffer = static_alloc(size, DEFAULT_ALIGNMENT);

    //3、检查分配是否成功
    if (rb->buffer == NULL) {
        rb->front_index = rb->rear_index = 0;
        rb->size = 0;
        rb->isPowerOfTwo_Size = false;
        return RET_E_NO_MEM;
    }

    //4、分配成功
    rb->front_index = 0;
    rb->rear_index = 0;
    rb->size = size;
    rb->isPowerOfTwo_Size = (rb->size != 0) && ((rb->size & (rb->size - 1)) == 0);
    // 简洁且安全地判断是否是2的幂; //判断缓冲区大小是不是2得幂 用于高效判断
    return RET_OK;
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
    rb_isr_state_t saved;
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
    rb_isr_state_t saved;
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
ret_code_t WriteRingBuffer(RingBuffer *rb, const uint8_t *add, uint32_t *size, const uint8_t isForceWrite) {
    //1、参数合法性检查
    if (rb == NULL || add == NULL || *size == 0 || rb->buffer == NULL || rb->size < 2)return RET_E_INVALID_ARG;
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
            return RET_E_NO_MEM;
        }
    }

    // 如果窥视大小为0（可能在isForcePeek后发生），则直接成功返回
    if (*size == 0) {
        RB_EXIT_CRITICAL();
        return RET_OK;
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
    return RET_OK;
}

/**
 *
 * @param rb 环形缓冲区指针
 * @param add 接收数据的地址
 * @param size 要获取的数据大小（字节
 * @param isForceRead 是否在数据不足 @param size 大小时候强制读取已有的全部数据
 * @return 返回是否读取成功
 */
ret_code_t ReadRingBuffer(RingBuffer *rb, uint8_t *add, uint32_t *size, const uint8_t isForceRead) {
    //1、参数合法性检查
    if (rb == NULL || add == NULL || *size == 0 || rb->buffer == NULL || rb->size < 2)return RET_E_INVALID_ARG;
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
            return RET_E_DATA_NOT_ENOUGH;
        }
    }

    // 如果窥视大小为0（可能在isForcePeek后发生），则直接成功返回
    if (*size == 0) {
        RB_EXIT_CRITICAL();
        return RET_OK;
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
    return RET_OK;
}

/**
 * @brief  窥视环形缓冲区中的数据，但不移动读指针
 * @param  rb          环形缓冲区指针
 * @param  add         接收数据的地址
 * @param  size        要窥视的数据大小（字节）
 * @param  isForcePeek 是否在数据不足时强制窥视已有的数据
 * @return 返回是否成功窥视（如果请求的数据量大于已用空间且非强制，则失败）
 */
ret_code_t PeekRingBuffer(RingBuffer *rb, uint8_t *add, uint32_t *size, const uint8_t isForcePeek) {
    // 1、参数合法性检查
    if (rb == NULL || add == NULL || *size == 0 || rb->buffer == NULL || rb->size < 2)return RET_E_INVALID_ARG;

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
            return RET_E_DATA_NOT_ENOUGH;
        }
    }

    // 如果窥视大小为0（可能在isForcePeek后发生），则直接成功返回
    if (*size == 0) {
        RB_EXIT_CRITICAL();
        return RET_OK;
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
    return RET_OK;
}

/**
 * @brief  添加数据到环形缓冲区 中断版本
 * @param rb 环形缓冲区指针
 * @param add 数据源地址
 * @param size 添加的数据大小（字节）
 * @param isForceWrite 是否强制写入可写入得长度数据剩余丢弃
 * @return 返回是否成功
 */
ret_code_t WriteRingBufferFromISR(RingBuffer *rb, const uint8_t *add, uint32_t *size, const uint8_t isForceWrite) {
    //1、参数合法性检查
    if (rb == NULL || add == NULL || *size == 0 || rb->buffer == NULL || rb->size < 2)return RET_E_INVALID_ARG;
    // --- 进入临界区，保护所有对 rb 成员的访问 ---
    rb_isr_state_t saved;
    RB_ENTER_CRITICAL_FROM_ISR(saved);
    //2、检查当前缓冲区的大小是否能够装入
    const uint32_t remain_size = RingBuffer_GetRemainSize_Internal(rb);
    if (remain_size < *size) {
        if (isForceWrite) {
            *size = remain_size;
        } else {
            // --- 在返回前，退出临界区 ---
            RB_EXIT_CRITICAL_FROM_ISR(saved);
            return RET_E_NO_MEM;
        }
    }

    // 如果窥视大小为0（可能在isForcePeek后发生），则直接成功返回
    if (*size == 0) {
        RB_EXIT_CRITICAL_FROM_ISR(saved);
        return RET_OK;
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
    return RET_OK;
}

/**
 *
 * @param rb 环形缓冲区指针中断版本
 * @param add 接收数据的地址
 * @param size 要获取的数据大小（字节
 * @param isForceRead 是否在数据不足 @param size 大小时候强制读取已有的全部数据
 * @return 返回是否读取成功
 */
ret_code_t ReadRingBufferFromISR(RingBuffer *rb, uint8_t *add, uint32_t *size, const uint8_t isForceRead) {
    //1、参数合法性检查
    if (rb == NULL || add == NULL || *size == 0 || rb->buffer == NULL || rb->size < 2)return RET_E_INVALID_ARG;
    // --- 进入临界区，保护所有对 rb 成员的访问 ---
    rb_isr_state_t saved;
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
            return RET_E_DATA_NOT_ENOUGH;
        }
    }

    // 如果窥视大小为0（可能在isForcePeek后发生），则直接成功返回
    if (*size == 0) {
        RB_EXIT_CRITICAL_FROM_ISR(saved);
        return RET_OK;
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
    return RET_OK;
}

/**
 * @brief 重置缓冲区
 * @param rb 句柄
 * @return
 */
ret_code_t ResetRingBuffer(RingBuffer *rb) {
    if (rb == NULL) return RET_E_INVALID_ARG;
    RB_ENTER_CRITICAL();
    rb->front_index = 0;
    rb->rear_index = 0;
    RB_EXIT_CRITICAL();
    return RET_OK;
}

/**
 * @brief 重置缓冲区 中断版本
 * @param rb 句柄
 * @return
 */
ret_code_t ResetRingBufferFromISR(RingBuffer *rb) {
    if (rb == NULL) {
        return RET_E_INVALID_ARG;
    }
    rb_isr_state_t saved;
    RB_ENTER_CRITICAL_FROM_ISR(saved);
    rb->front_index = 0;
    rb->rear_index = 0;
    RB_EXIT_CRITICAL_FROM_ISR(saved);
    return RET_OK;
}

/**
 *
 * @param rb 缓冲区句柄
 * @param want 想要获取的大小
 * @param out  实际输出的窗口
 * @param granted 实际大小
 * @param isCompatible  是否部分存入
 * @return 是否成功
 */
ret_code_t RingBuffer_WriteReserve(RingBuffer *rb,
                             uint32_t want,
                             RingBufferSpan *out,
                             uint32_t *granted,
                             bool isCompatible) {
    if (!rb || !out || !granted || !rb->buffer || rb->size < 2) return RET_E_INVALID_ARG;

    RB_ENTER_CRITICAL();

    /* 1、获取剩余空间大小 */
    const uint32_t remain = RingBuffer_GetRemainSize_Internal(rb);

    uint32_t g = want;

    if (g == 0) {
        out->p1 = NULL;
        out->n1 = 0;
        out->p2 = NULL;
        out->n2 = 0;
        *granted = 0;
        RB_EXIT_CRITICAL();
        return RET_OK;
    }

    if (g > remain) {
        if (isCompatible) g = remain;
        else {
            RB_EXIT_CRITICAL();
            return RET_E_NO_MEM;
        }
    }

    const uint32_t rear = rb->rear_index;
    const uint32_t front = rb->front_index;
    const uint32_t size = rb->size;

    uint32_t n1 = 0, n2 = 0;

    if (rear < front) {
        // 可写连续段：rear .. front-1 之间（预留 1 字节区分满/空）
        const uint32_t seg = front - rear - 1;
        n1 = (g < seg) ? g : seg;
        n2 = 0;

        out->p1 = (n1 > 0) ? (rb->buffer + rear) : NULL;
        out->n1 = n1;
        out->p2 = NULL;
        out->n2 = 0;
        *granted = n1 + n2; // 注意：这里最多只能给 n1（因为不允许跨 front）
        RB_EXIT_CRITICAL();
        return RET_OK;
    } else {
        // 空洞跨越末尾：先尾段再头段
        const uint32_t tailFree = (front == 0) ? (size - rear - 1) : (size - rear);
        n1 = (g < tailFree) ? g : tailFree;
        n2 = g - n1;

        out->p1 = (n1 > 0) ? (rb->buffer + rear) : NULL;
        out->n1 = n1;
        out->p2 = (n2 > 0) ? (rb->buffer) : NULL;
        out->n2 = n2;
        *granted = n1 + n2;

        RB_EXIT_CRITICAL();
        return RET_OK;
    }
}

/**
 *
 * @param rb 操作句柄
 * @param commit 实际写入的次数
 * @return 成功或者失败
 */
ret_code_t RingBuffer_WriteCommit(RingBuffer *rb, uint32_t commit) {
    if (!rb || !rb->buffer || rb->size < 2) return RET_E_INVALID_ARG;

    RB_ENTER_CRITICAL();
    const uint32_t remain = RingBuffer_GetRemainSize_Internal(rb);
    if (commit > remain) {
        RB_EXIT_CRITICAL();
        return RET_E_NO_MEM;
    }

    if (rb->isPowerOfTwo_Size) rb->rear_index = (rb->rear_index + commit) & (rb->size - 1);
    else rb->rear_index = (rb->rear_index + commit) % rb->size;

    RB_EXIT_CRITICAL();
    return RET_OK;
}


/**
 *
 * @param rb 缓冲区句柄
 * @param want 想要获取的大小
 * @param out  实际输出的窗口
 * @param granted 实际大小
 * @param isCompatible  是否部分存入
 * @return 是否成功
 */
ret_code_t RingBuffer_WriteReserveFromISR(RingBuffer *rb,
                                    uint32_t want,
                                    RingBufferSpan *out,
                                    uint32_t *granted,
                                    bool isCompatible) {
    if (!rb || !out || !granted || !rb->buffer || rb->size < 2) return RET_E_INVALID_ARG;

    rb_isr_state_t saved;
    RB_ENTER_CRITICAL_FROM_ISR(saved);

    /* 1、获取剩余空间大小 */
    const uint32_t remain = RingBuffer_GetRemainSize_Internal(rb);

    uint32_t g = want;

    if (g == 0) {
        out->p1 = NULL;
        out->n1 = 0;
        out->p2 = NULL;
        out->n2 = 0;
        *granted = 0;
        RB_EXIT_CRITICAL_FROM_ISR(saved);
        return RET_OK; // 或 return false; 取决于你接口约定
    }

    if (g > remain) {
        if (isCompatible) g = remain;
        else {
            RB_EXIT_CRITICAL_FROM_ISR(saved);
            return RET_E_NO_MEM;
        }
    }

    const uint32_t rear = rb->rear_index;
    const uint32_t front = rb->front_index;
    const uint32_t size = rb->size;

    uint32_t n1 = 0, n2 = 0;

    if (rear < front) {
        // 可写连续段：rear .. front-1 之间（预留 1 字节区分满/空）
        const uint32_t seg = front - rear - 1;
        n1 = (g < seg) ? g : seg;
        n2 = 0;

        out->p1 = (n1 > 0) ? (rb->buffer + rear) : NULL;
        out->n1 = n1;
        out->p2 = NULL;
        out->n2 = 0;
        *granted = n1 + n2; // 注意：这里最多只能给 n1（因为不允许跨 front）
        RB_EXIT_CRITICAL_FROM_ISR(saved);
        return RET_OK;
    } else {
        // 空洞跨越末尾：先尾段再头段
        const uint32_t tailFree = (front == 0) ? (size - rear - 1) : (size - rear);
        n1 = (g < tailFree) ? g : tailFree;
        n2 = g - n1;

        out->p1 = (n1 > 0) ? (rb->buffer + rear) : NULL;
        out->n1 = n1;
        out->p2 = (n2 > 0) ? (rb->buffer) : NULL;
        out->n2 = n2;
        *granted = n1 + n2;

        RB_EXIT_CRITICAL_FROM_ISR(saved);
        return RET_OK;
    }
}

/**
 *
 * @param rb 操作句柄
 * @param commit 实际写入的次数
 * @return 成功或者失败
 */
ret_code_t RingBuffer_WriteCommitFromISR(RingBuffer *rb, uint32_t commit) {
    if (!rb || !rb->buffer || rb->size < 2) return RET_E_INVALID_ARG;

    rb_isr_state_t saved;
    RB_ENTER_CRITICAL_FROM_ISR(saved);

    const uint32_t remain = RingBuffer_GetRemainSize_Internal(rb);
    if (commit > remain) {
        RB_EXIT_CRITICAL_FROM_ISR(saved);
        return RET_E_NO_MEM;
    }

    if (rb->isPowerOfTwo_Size) rb->rear_index = (rb->rear_index + commit) & (rb->size - 1);
    else rb->rear_index = (rb->rear_index + commit) % rb->size;

    RB_EXIT_CRITICAL_FROM_ISR(saved);
    return RET_OK;
}

/**
 *
 * @param rb 创作句柄
 * @param want 想要多少空间
 * @param out 返回的窗口
 * @param granted 实际获得多少空间
 * @param isCompatible 是否启用兼容模式
 * @return 成功或者失败
 */
ret_code_t RingBuffer_ReadReserve(RingBuffer *rb,
                            uint32_t want,
                            RingBufferSpan *out,
                            uint32_t *granted,
                            bool isCompatible) {
    if (!rb || !out || !granted || !rb->buffer || rb->size < 2) return RET_E_INVALID_ARG;

    // 约定：want==0 直接视为成功但授予0（你也可以选择直接 return false，但要一致）
    if (want == 0) {
        out->p1 = NULL;
        out->n1 = 0;
        out->p2 = NULL;
        out->n2 = 0;
        *granted = 0;
        return RET_OK;
    }

    RB_ENTER_CRITICAL();

    const uint32_t used = RingBuffer_GetUsedSize_Internal(rb);

    uint32_t g = want;
    if (g > used) {
        if (isCompatible) g = used;
        else {
            RB_EXIT_CRITICAL();
            return RET_E_DATA_NOT_ENOUGH;
        }
    }

    /* 若 used==0 且兼容模式，则 g 可能变为0：按成功但授予0处理 */
    if (g == 0) {
        out->p1 = NULL;
        out->n1 = 0;
        out->p2 = NULL;
        out->n2 = 0;
        *granted = 0;
        RB_EXIT_CRITICAL();
        return RET_OK;
    }

    const uint32_t front = rb->front_index;
    const uint32_t rear = rb->rear_index;
    const uint32_t size = rb->size;

    if (front < rear) {
        /* 连续可读：front..rear-1 */
        const uint32_t seg = rear - front;
        const uint32_t n1 = (g < seg) ? g : seg;
        out->p1 = rb->buffer + front;
        out->n1 = n1;
        out->p2 = NULL;
        out->n2 = 0;
        *granted = out->n1 + out->n2; // == n1
    } else {
        /* 跨尾：先读到末尾，再从头读到 rear-1 */
        const uint32_t tailAvail = size - front;
        const uint32_t n1 = (g < tailAvail) ? g : tailAvail;
        const uint32_t n2 = g - n1;

        out->p1 = (n1 > 0) ? (rb->buffer + front) : NULL;
        out->n1 = n1;
        out->p2 = (n2 > 0) ? rb->buffer : NULL;
        out->n2 = n2;
        *granted = n1 + n2;
    }

    RB_EXIT_CRITICAL();
    return RET_OK;
}

/**
 *
 * @param rb 创作句柄
 * @param want 想要多少空间
 * @param out 返回的窗口
 * @param granted 实际获得多少空间
 * @param isCompatible 是否启用兼容模式
 * @return 成功或者失败
 */
ret_code_t RingBuffer_ReadReserveFromISR(RingBuffer *rb,
                                   uint32_t want,
                                   RingBufferSpan *out,
                                   uint32_t *granted,
                                   bool isCompatible) {
    if (!rb || !out || !granted || !rb->buffer || rb->size < 2) return RET_E_INVALID_ARG;

    // 约定：want==0 直接视为成功但授予0（你也可以选择直接 return false，但要一致）
    if (want == 0) {
        out->p1 = NULL;
        out->n1 = 0;
        out->p2 = NULL;
        out->n2 = 0;
        *granted = 0;
        return RET_OK;
    }

    rb_isr_state_t saved;
    RB_ENTER_CRITICAL_FROM_ISR(saved);

    const uint32_t used = RingBuffer_GetUsedSize_Internal(rb);

    uint32_t g = want;
    if (g > used) {
        if (isCompatible) g = used;
        else {
            RB_EXIT_CRITICAL_FROM_ISR(saved);
            return RET_E_DATA_NOT_ENOUGH;
        }
    }

    /* 若 used==0 且兼容模式，则 g 可能变为0：按成功但授予0处理 */
    if (g == 0) {
        out->p1 = NULL;
        out->n1 = 0;
        out->p2 = NULL;
        out->n2 = 0;
        *granted = 0;
        RB_EXIT_CRITICAL_FROM_ISR(saved);
        return RET_OK;
    }

    const uint32_t front = rb->front_index;
    const uint32_t rear = rb->rear_index;
    const uint32_t size = rb->size;

    if (front < rear) {
        /* 连续可读：front..rear-1 */
        const uint32_t seg = rear - front;
        const uint32_t n1 = (g < seg) ? g : seg;

        out->p1 = rb->buffer + front;
        out->n1 = n1;
        out->p2 = NULL;
        out->n2 = 0;
        *granted = out->n1 + out->n2; // == n1
    } else {
        /* 跨尾：先读到末尾，再从头读到 rear-1 */
        const uint32_t tailAvail = size - front;
        const uint32_t n1 = (g < tailAvail) ? g : tailAvail;
        const uint32_t n2 = g - n1;

        out->p1 = (n1 > 0) ? (rb->buffer + front) : NULL;
        out->n1 = n1;
        out->p2 = (n2 > 0) ? rb->buffer : NULL;
        out->n2 = n2;
        *granted = n1 + n2;
    }

    RB_EXIT_CRITICAL_FROM_ISR(saved);
    return RET_OK;
}

/**
 *
 * @param rb 操作句柄
 * @param commit 提交读取的字节数
 * @return 成功或者失败
 */
ret_code_t RingBuffer_ReadCommit(RingBuffer *rb, uint32_t commit) {
    if (!rb || !rb->buffer || rb->size < 2) return RET_E_INVALID_ARG;

    RB_ENTER_CRITICAL();
    const uint32_t used = RingBuffer_GetUsedSize_Internal(rb);
    if (commit > used) {
        RB_EXIT_CRITICAL();
        return RET_E_DATA_NOT_ENOUGH;
    }

    if (rb->isPowerOfTwo_Size) rb->front_index = (rb->front_index + commit) & (rb->size - 1);
    else rb->front_index = (rb->front_index + commit) % rb->size;

    RB_EXIT_CRITICAL();
    return RET_OK;
}

/**
 *
 * @param rb 操作句柄
 * @param commit 提交读取的字节数
 * @return 成功或者失败
 */
ret_code_t RingBuffer_ReadCommitFromISR(RingBuffer *rb, uint32_t commit) {
    if (!rb || !rb->buffer || rb->size < 2) return RET_E_INVALID_ARG;

    rb_isr_state_t saved;
    RB_ENTER_CRITICAL_FROM_ISR(saved);
    const uint32_t used = RingBuffer_GetUsedSize_Internal(rb);
    if (commit > used) {
        RB_EXIT_CRITICAL_FROM_ISR(saved);
        return RET_E_DATA_NOT_ENOUGH;
    }

    if (rb->isPowerOfTwo_Size) rb->front_index = (rb->front_index + commit) & (rb->size - 1);
    else rb->front_index = (rb->front_index + commit) % rb->size;

    RB_EXIT_CRITICAL_FROM_ISR(saved);
    return RET_OK;
}

/**
 * @brief 丢弃指定字节数据
 * @param rb 操作句柄
 * @param drop 期望丢弃字节数
 * @param dropped 实际丢弃字节数
 * @param isCompatible 是否开启兼容模式
 * @return 成功或者失败
 * @note  兼容模式下 used为0会返回true
 */
ret_code_t RingBuffer_Drop(RingBuffer *rb, uint32_t drop, uint32_t *dropped, bool isCompatible) {
    if (!rb || !rb->buffer || rb->size < 2 || !dropped) return RET_E_INVALID_ARG;

    /* 1、判断剩余的存储字节数 */
    RB_ENTER_CRITICAL();
    const uint32_t used = RingBuffer_GetUsedSize_Internal(rb);
    if (drop == 0) {
        *dropped = 0;
        RB_EXIT_CRITICAL();
        return RET_OK;
    }
    /* 2、实际要丢失多少字节 */
    uint32_t g = drop;
    if (g > used) {
        if (isCompatible) g = used;
        else {
            *dropped = 0;
            RB_EXIT_CRITICAL();
            return RET_E_DATA_NOT_ENOUGH;
        }
    }

    /* 3、开始丢弃 */
    if (rb->isPowerOfTwo_Size) rb->front_index = (rb->front_index + g) & (rb->size - 1);
    else rb->front_index = (rb->front_index + g) % rb->size;
    *dropped = g;
    RB_EXIT_CRITICAL();
    return RET_OK;
}

/**
 * @brief 丢弃指定字节数据
 * @param rb 操作句柄
 * @param drop 期望丢弃字节数
 * @param dropped 实际丢弃字节数
 * @param isCompatible 是否开启兼容模式
 * @return 成功或者失败
 * @note  兼容模式下 used为0会返回true
 */
ret_code_t RingBuffer_DropFromISR(RingBuffer *rb, uint32_t drop, uint32_t *dropped, bool isCompatible) {
    if (!rb || !rb->buffer || rb->size < 2 || !dropped) return RET_E_INVALID_ARG;

    /* 1、判断剩余的存储字节数 */

    rb_isr_state_t saved;
    RB_ENTER_CRITICAL_FROM_ISR(saved);
    const uint32_t used = RingBuffer_GetUsedSize_Internal(rb);
    if (drop == 0) {
        *dropped = 0;
        RB_EXIT_CRITICAL_FROM_ISR(saved);
        return RET_OK;
    }
    /* 2、实际要丢失多少字节 */
    uint32_t g = drop;
    if (g > used) {
        if (isCompatible) g = used;
        else {
            *dropped = 0;
            RB_EXIT_CRITICAL_FROM_ISR(saved);
            return RET_E_DATA_NOT_ENOUGH;
        }
    }

    /* 3、开始丢弃 */
    if (rb->isPowerOfTwo_Size) rb->front_index = (rb->front_index + g) & (rb->size - 1);
    else rb->front_index = (rb->front_index + g) % rb->size;
    *dropped = g;
    RB_EXIT_CRITICAL_FROM_ISR(saved);
    return RET_OK;
}



#endif