#include "APP_config.h"

#if defined(ENABLE_RINGBUFFER_SYSTEM)
#include <stdbool.h>
#include <string.h>

#include "MemoryAllocation.h"
#include "RingBuffer.h"
#include "rb_port.h"

/* 状态错误状态码打包宏 */

#define RB_RET(clas_, err_) RET_MAKE(RET_MOD_RB, RET_SUB_RB_CORE, RET_CODE_MAKE((clas_), (err_)))

/* 统一参数校验宏 */
#define RB_CHECK_VALID(rb)                                          \
    do {                                                            \
        if ((rb) == NULL || (rb)->buffer == NULL || (rb)->size < 2) \
            return RB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);      \
    } while (0)

#define RB_CHECK_ARGS(rb, ptr, size)                                                              \
    do {                                                                                          \
        RB_CHECK_VALID(rb);                                                                       \
        if (!(ptr) || !(size) || *(size) == 0) return RB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG); \
    } while (0)

/**
 * @brief         将数据入队/出队后的位置更新
 * @param rb      RB句柄
 * @param current 当前位置
 * @param step    想要入队或者出队的步进数
 * @return        返回新的所引位置
 */
static inline uint32_t RB_NextIndex(const RingBuffer* rb, uint32_t current, uint32_t step) {
    if (rb->isPowerOfTwo_Size) {
        return (current + step) & (rb->size - 1);
    }
    return (current + step) % rb->size;
}

/**
 * @brief    内部辅助函数计算当前RB空间使用了多少字节
 * @param rb RB句柄
 * @return   使用的字节数
 */
static inline uint32_t RB_GetUsed_Core(const RingBuffer* rb) {
    if (rb->isPowerOfTwo_Size) {
        return (rb->rear_index - rb->front_index + rb->size) & (rb->size - 1);
    }
    return (rb->rear_index - rb->front_index + rb->size) % rb->size;
}

/**
 * @brief    计算内部可使用的字节数
 * @param rb RB句柄
 * @return   内部可使用的字节数
 */
static inline uint32_t RB_GetRemain_Core(const RingBuffer* rb) {
    return rb->size - RB_GetUsed_Core(rb) - 1;
}

/**
 * @brief 内部写
 * @param rb RB句柄
 * @param add 需要写多少字节数
 * @param size 实际上写入的字节数
 * @param isForce true 有多少空间写多少 false 必须能全部写入才写入
 * @return 状态码
 */
static ret_code_t RB_Write_Logic(RingBuffer* rb, const uint8_t* add, uint32_t* size, bool isForce) {
    const uint32_t remain = RB_GetRemain_Core(rb);

    if (remain < *size) {
        if (isForce)
            *size = remain;
        else
            return RB_RET(RET_CLASS_RESOURCE, RET_R_NO_MEM);
    }
    if (*size == 0) return RET_OK;

    const uint32_t end_size = rb->size - rb->rear_index;
    if (end_size >= *size) {
        memcpy(rb->buffer + rb->rear_index, add, *size);
    } else {
        memcpy(rb->buffer + rb->rear_index, add, end_size);
        memcpy(rb->buffer, add + end_size, *size - end_size);
    }
    rb->rear_index = RB_NextIndex(rb, rb->rear_index, *size);
    return RET_OK;
}

/**
 * @brief 内部读取数据
 * @param rb RB句柄
 * @param add 想要读取的大小
 * @param size 实际上读取的大小
 * @param isForce true 有多少读多少 false 在有想要的数据大小数才读取
 * @param isPeek  true 读取后但是不消耗数据 false 读取后消耗数据
 * @return 状态码
 */
static ret_code_t RB_Read_Logic(RingBuffer* rb, uint8_t* add, uint32_t* size, bool isForce,
                                bool isPeek) {
    const uint32_t used = RB_GetUsed_Core(rb);

    if (used < *size) {
        if (isForce)
            *size = used;
        else
            return RB_RET(RET_CLASS_DATA, RET_R_DATA_NOT_ENOUGH);
    }
    if (*size == 0) return RET_OK;

    const uint32_t end_size = rb->size - rb->front_index;
    if (end_size >= *size) {
        memcpy(add, rb->buffer + rb->front_index, *size);
    } else {
        memcpy(add, rb->buffer + rb->front_index, end_size);
        memcpy(add + end_size, rb->buffer, *size - end_size);
    }

    if (!isPeek) {
        rb->front_index = RB_NextIndex(rb, rb->front_index, *size);
    }
    return RET_OK;
}

/**
 *
 * @param rb RB句柄
 * @param want 想要读写的数据大小
 * @param out  输出可以读写的窗口数据结构体指针
 * @param granted 实际批准的大小
 * @param isCompatible true 读写是有多少就读写多少 false
 * 必须满足想要的数据大小才能获取读写的窗口数据
 * @param isWrite  区分当前是需要读还是写的窗口数据
 * @return 状态码
 */
static ret_code_t RB_Reserve_Logic(const RingBuffer* rb, uint32_t want, RingBufferSpan* out,
                                   uint32_t* granted, bool isCompatible, bool isWrite) {
    const uint32_t limit = isWrite ? RB_GetRemain_Core(rb) : RB_GetUsed_Core(rb);

    // 0请求处理
    if (want == 0) {
        out->p1 = out->p2 = NULL;
        out->n1 = out->n2 = *granted = 0;
        return RET_OK;
    }

    // 空间检查
    if (want > limit) {
        if (isCompatible)
            want = limit;
        else
            return isWrite ? RB_RET(RET_CLASS_RESOURCE, RET_R_NO_MEM)
                           : RB_RET(RET_CLASS_DATA, RET_R_DATA_NOT_ENOUGH);
    }

    // 兼容模式下如果限制为0
    if (want == 0) {
        out->p1 = out->p2 = NULL;
        out->n1 = out->n2 = *granted = 0;
        return RET_OK;
    }

    // 简化逻辑：直接利用索引位置计算
    uint32_t n1 = 0, n2 = 0;

    if (isWrite) {
        // 写模式
        if (rb->rear_index < rb->front_index) {
            // 中间空闲：rear ... front-1
            const uint32_t seg = rb->front_index - rb->rear_index - 1;
            n1                 = (want < seg) ? want : seg;
        } else {
            // 两头空闲：rear ... end, 0 ... front-1
            uint32_t tailFree = rb->size - rb->rear_index;
            if (rb->front_index == 0) tailFree--;  // 如果front在0，rear不能写到最后

            n1 = (want < tailFree) ? want : tailFree;
            n2 = want - n1;
        }
        out->p1 = (n1 > 0) ? (rb->buffer + rb->rear_index) : NULL;
        out->p2 = (n2 > 0) ? (rb->buffer) : NULL;  // Wrap around to start
    } else {
        // 读模式
        if (rb->front_index < rb->rear_index) {
            // 连续数据：front ... rear
            const uint32_t seg = rb->rear_index - rb->front_index;
            n1                 = (want < seg) ? want : seg;
        } else {
            // 跨尾数据：front ... end, 0 ... rear
            const uint32_t tailAvail = rb->size - rb->front_index;
            n1                       = (want < tailAvail) ? want : tailAvail;
            n2                       = want - n1;
        }
        out->p1 = (n1 > 0) ? (rb->buffer + rb->front_index) : NULL;
        out->p2 = (n2 > 0) ? (rb->buffer) : NULL;
    }

    out->n1  = n1;
    out->n2  = n2;
    *granted = n1 + n2;
    return RET_OK;
}

/**
 * @brief 创建指定大小的RB
 * @param rb RB句柄
 * @param name RB名称
 * @param size 容量大小 字节单位
 * @return 状态码
 */
ret_code_t CreateRingBuffer(RingBuffer* rb, const char* name, const uint32_t size) {
    if (rb == NULL || size < 2) return RB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);

    rb->buffer = static_alloc(size, DEFAULT_ALIGNMENT);
    if (rb->buffer == NULL) {
        memset(rb, 0, sizeof(RingBuffer));
        return RB_RET(RET_CLASS_RESOURCE, RET_R_NO_MEM);
    }

    rb->name        = name;
    rb->front_index = rb->rear_index = 0;
    rb->size                         = size;
    rb->isPowerOfTwo_Size            = (size != 0) && ((size & (size - 1)) == 0);
    return RET_OK;
}

/**
 * @brief 线程版获取使用的容量字节数
 * @param rb RB句柄
 * @return 已经使用的容量字节数
 */
uint32_t RingBuffer_GetUsedSize(const RingBuffer* rb) {
    RB_CHECK_VALID(rb);
    RB_ENTER_CRITICAL();
    const uint32_t ret = RB_GetUsed_Core(rb);
    RB_EXIT_CRITICAL();
    return ret;
}
/**
 * @brief 中断版获取使用的容量字节数
 * @param rb RB句柄
 * @return 已经使用的容量字节数
 */
uint32_t RingBuffer_GetUsedSizeFromISR(const RingBuffer* rb) {
    RB_CHECK_VALID(rb);
    rb_isr_state_t saved;
    RB_ENTER_CRITICAL_FROM_ISR(saved);
    const uint32_t ret = RB_GetUsed_Core(rb);
    RB_EXIT_CRITICAL_FROM_ISR(saved);
    return ret;
}

/**
 * @brief 线程版获取剩余容量字字节数
 * @param rb RB句柄
 * @return 返回剩余容量字节数
 */
uint32_t RingBuffer_GetRemainSize(const RingBuffer* rb) {
    RB_CHECK_VALID(rb);
    RB_ENTER_CRITICAL();
    const uint32_t ret = RB_GetRemain_Core(rb);
    RB_EXIT_CRITICAL();
    return ret;
}
/**
 * @brief 中断版获取剩余容量字节数
 * @param rb RB句柄
 * @return 剩余容量字节数
 */
uint32_t RingBuffer_GetRemainSizeFromISR(const RingBuffer* rb) {
    RB_CHECK_VALID(rb);
    rb_isr_state_t saved;
    RB_ENTER_CRITICAL_FROM_ISR(saved);
    const uint32_t ret = RB_GetRemain_Core(rb);
    RB_EXIT_CRITICAL_FROM_ISR(saved);
    return ret;
}

/**
 * @brief 线程版 写入数据到 RB
 * @param rb RB句柄
 * @param add 想要添加的字节数
 * @param size 实际添加的字节数
 * @param isForceWrite true 有多少空间就写多少数据 false 必须全部能够装下才能写入
 * @return 32位分段状态码
 */
ret_code_t WriteRingBuffer(RingBuffer* rb, const uint8_t* add, uint32_t* size,
                           const uint8_t isForceWrite) {
    RB_CHECK_ARGS(rb, add, size);
    RB_ENTER_CRITICAL();
    const ret_code_t ret = RB_Write_Logic(rb, add, size, isForceWrite);
    RB_EXIT_CRITICAL();
    return ret;
}
/**
 * @brief 中断版 写入数据到 RB
 * @param rb RB句柄
 * @param add 想要添加的字节数
 * @param size 实际添加的字节数
 * @param isForceWrite true 有多少空间就写多少数据 false 必须全部能够装下才能写入
 * @return 32位分段状态码
 */
ret_code_t WriteRingBufferFromISR(RingBuffer* rb, const uint8_t* add, uint32_t* size,
                                  const uint8_t isForceWrite) {
    RB_CHECK_ARGS(rb, add, size);
    rb_isr_state_t saved;
    RB_ENTER_CRITICAL_FROM_ISR(saved);
    const ret_code_t ret = RB_Write_Logic(rb, add, size, isForceWrite);
    RB_EXIT_CRITICAL_FROM_ISR(saved);
    return ret;
}

/**
 * @brief 线程版 读取 RB 数据
 * @param rb RB句柄
 * @param add 想要读取的字节数
 * @param size 实际读取的字计数
 * @param isForceRead true 在数据不够时有多少读多少 false 必须有足够数据才读取
 * @return 32位分段状态码
 */
ret_code_t ReadRingBuffer(RingBuffer* rb, uint8_t* add, uint32_t* size, const uint8_t isForceRead) {
    RB_CHECK_ARGS(rb, add, size);
    RB_ENTER_CRITICAL();
    const ret_code_t ret = RB_Read_Logic(rb, add, size, isForceRead, false);
    RB_EXIT_CRITICAL();
    return ret;
}
/**
 * @brief 中断版 读取 RB 数据
 * @param rb RB句柄
 * @param add 想要读取的字节数
 * @param size 实际读取的字计数
 * @param isForceRead true 在数据不够时有多少读多少 false 必须有足够数据才读取
 * @return 32位分段状态码
 */
ret_code_t ReadRingBufferFromISR(RingBuffer* rb, uint8_t* add, uint32_t* size,
                                 const uint8_t isForceRead) {
    RB_CHECK_ARGS(rb, add, size);
    rb_isr_state_t saved;
    RB_ENTER_CRITICAL_FROM_ISR(saved);
    const ret_code_t ret = RB_Read_Logic(rb, add, size, isForceRead, false);
    RB_EXIT_CRITICAL_FROM_ISR(saved);
    return ret;
}

/**
 * @brief 线程版 窥视 RB 数据 但是不消耗数据
 * @param rb RB句柄
 * @param add 想要读取的字节数
 * @param size 实际读取的字计数
 * @param isForcePeek true 在数据不够时有多少读多少 false 必须有足够数据才读取
 * @return 32位分段状态码
 */
ret_code_t PeekRingBuffer(const RingBuffer* rb, uint8_t* add, uint32_t* size,
                          const uint8_t isForcePeek) {
    RB_CHECK_ARGS(rb, add, size);
    // Cast away const specifically for lock (standard practice)
    RingBuffer* rb_mut = (RingBuffer*)rb;
    RB_ENTER_CRITICAL();
    const ret_code_t ret = RB_Read_Logic(rb_mut, add, size, isForcePeek, true);
    RB_EXIT_CRITICAL();
    return ret;
}

/**
 * @beief 线程版 重置 RB 空间
 * @param rb RB句柄
 * @return 状体码
 */
ret_code_t ResetRingBuffer(RingBuffer* rb) {
    RB_CHECK_VALID(rb);
    RB_ENTER_CRITICAL();
    rb->front_index = rb->rear_index = 0;
    RB_EXIT_CRITICAL();
    return RET_OK;
}
/**
 * @beief 中断版 重置 RB 空间
 * @param rb RB句柄
 * @return 状体码
 */
ret_code_t ResetRingBufferFromISR(RingBuffer* rb) {
    RB_CHECK_VALID(rb);
    rb_isr_state_t saved;
    RB_ENTER_CRITICAL_FROM_ISR(saved);
    rb->front_index = rb->rear_index = 0;
    RB_EXIT_CRITICAL_FROM_ISR(saved);
    return RET_OK;
}

/**
 * @brief 线程版 获取可写空间
 * @param rb RB句柄
 * @param want 想要申请的空间
 * @param out 保存申请空间的相关信息
 * @param granted 时机批准的大小
 * @param isCompatible true 有多少批准多少 false 必须有足够的空间才批准
 * @note 注意获取空间后没有存储划分的空间信息， 需要注意不要并发 只能单生产者
 * @return 状态码
 */
ret_code_t RingBuffer_WriteReserve(RingBuffer* rb, uint32_t want, RingBufferSpan* out,
                                   uint32_t* granted, bool isCompatible) {
    if (!rb || !out || !granted || !rb->buffer || rb->size < 2)
        return RB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    RB_ENTER_CRITICAL();
    const ret_code_t ret = RB_Reserve_Logic(rb, want, out, granted, isCompatible, true);
    RB_EXIT_CRITICAL();
    return ret;
}

/**
 * @brief 中断版 获取可写空间
 * @param rb RB句柄
 * @param want 想要申请的空间
 * @param out 保存申请空间的相关信息
 * @param granted 时机批准的大小
 * @param isCompatible true 有多少批准多少 false 必须有足够的空间才批准
 * @note 注意获取空间后没有存储划分的空间信息， 需要注意不要并发 只能单生产者
 * @return 状态码
 */
ret_code_t RingBuffer_WriteReserveFromISR(RingBuffer* rb, uint32_t want, RingBufferSpan* out,
                                          uint32_t* granted, bool isCompatible) {
    if (!rb || !out || !granted || !rb->buffer || rb->size < 2)
        return RB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    rb_isr_state_t saved;
    RB_ENTER_CRITICAL_FROM_ISR(saved);
    const ret_code_t ret = RB_Reserve_Logic(rb, want, out, granted, isCompatible, true);
    RB_EXIT_CRITICAL_FROM_ISR(saved);
    return ret;
}

/**
 * @brief 在获取空间信息并存入后， 使用本函数提交实际存入的大小
 * @param rb RB句柄
 * @param commit 提交的实际大小
 * @note 必须调用前 加锁或 临界区保证原子性
 * @return
 */
static inline ret_code_t RB_Commit_Write_Logic(RingBuffer* rb, uint32_t commit) {
    const uint32_t remain = RB_GetRemain_Core(rb);
    if (commit > remain) return RB_RET(RET_CLASS_RESOURCE, RET_R_NO_MEM);
    rb->rear_index = RB_NextIndex(rb, rb->rear_index, commit);
    return RET_OK;
}
/**
 * @brief 线程版 提交实际写入的大小
 * @param rb RB 句柄
 * @param commit 实际写入的大小
 * @return 状态码
 */
ret_code_t RingBuffer_WriteCommit(RingBuffer* rb, uint32_t commit) {
    RB_CHECK_VALID(rb);
    RB_ENTER_CRITICAL();
    const ret_code_t ret = RB_Commit_Write_Logic(rb, commit);
    RB_EXIT_CRITICAL();
    return ret;
}
/**
 * @brief 中断版 提交实际写入的大小
 * @param rb RB 句柄
 * @param commit 实际写入的大小
 * @return 状态码
 */
ret_code_t RingBuffer_WriteCommitFromISR(RingBuffer* rb, uint32_t commit) {
    RB_CHECK_VALID(rb);
    rb_isr_state_t saved;
    RB_ENTER_CRITICAL_FROM_ISR(saved);
    const ret_code_t ret = RB_Commit_Write_Logic(rb, commit);
    RB_EXIT_CRITICAL_FROM_ISR(saved);
    return ret;
}

/**
 * @brief 线程版 获取可读空间
 * @param rb RB句柄
 * @param want 想要申请的空间
 * @param out 保存申请空间的相关信息
 * @param granted 时机批准的大小
 * @param isCompatible true 有多少批准多少 false 必须有足够的空间才批准
 * @return 状态码
 */
ret_code_t RingBuffer_ReadReserve(RingBuffer* rb, uint32_t want, RingBufferSpan* out,
                                  uint32_t* granted, bool isCompatible) {
    if (!rb || !out || !granted || !rb->buffer || rb->size < 2)
        return RB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    RB_ENTER_CRITICAL();
    const ret_code_t ret = RB_Reserve_Logic(rb, want, out, granted, isCompatible, false);
    RB_EXIT_CRITICAL();
    return ret;
}
/**
 * @brief 中断版 获取可读空间
 * @param rb RB句柄
 * @param want 想要申请的空间
 * @param out 保存申请空间的相关信息
 * @param granted 时机批准的大小
 * @param isCompatible true 有多少批准多少 false 必须有足够的空间才批准
 * @return 状态码
 */
ret_code_t RingBuffer_ReadReserveFromISR(RingBuffer* rb, uint32_t want, RingBufferSpan* out,
                                         uint32_t* granted, bool isCompatible) {
    if (!rb || !out || !granted || !rb->buffer || rb->size < 2)
        return RB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    rb_isr_state_t saved;
    RB_ENTER_CRITICAL_FROM_ISR(saved);
    const ret_code_t ret = RB_Reserve_Logic(rb, want, out, granted, isCompatible, false);
    RB_EXIT_CRITICAL_FROM_ISR(saved);
    return ret;
}

/**
 * @brief 在获取空间信息并存入后， 使用本函数提交实际读取的大小
 * @param rb RB句柄
 * @param commit 提交的实际大小
 * @param actual_drop 实际丢弃的大小
 * @param isCompatible true 在数据大小不满足要求的情况下有多少丢多少 false
 * 必须在有足够的数据才能丢弃
 * @note 必须调用前 加锁或 临界区保证原子性
 * @return 状态码
 */
static inline ret_code_t RB_Commit_Read_Logic(RingBuffer* rb, uint32_t commit,
                                              uint32_t* actual_drop, bool isCompatible) {
    const uint32_t used = RB_GetUsed_Core(rb);
    uint32_t g          = commit;

    if (g > used) {
        if (isCompatible)
            g = used;
        else
            return RB_RET(RET_CLASS_DATA, RET_R_DATA_NOT_ENOUGH);
    }

    if (actual_drop) *actual_drop = g;
    if (g > 0) {
        rb->front_index = RB_NextIndex(rb, rb->front_index, g);
    }
    return RET_OK;
}

/**
 * @brief 线程版零拷贝 获取可读空间的信息
 * @param rb RB 句柄
 * @param commit 想要读取的字节数
 * @return 状态码
 */
ret_code_t RingBuffer_ReadCommit(RingBuffer* rb, uint32_t commit) {
    RB_CHECK_VALID(rb);
    RB_ENTER_CRITICAL();
    const ret_code_t ret = RB_Commit_Read_Logic(rb, commit, NULL, false);
    RB_EXIT_CRITICAL();
    return ret;
}
/**
 * @brief 中断版零拷贝 获取可读空间的信息
 * @param rb RB 句柄
 * @param commit 想要读取的字节数
 * @return 状态码
 */
ret_code_t RingBuffer_ReadCommitFromISR(RingBuffer* rb, uint32_t commit) {
    RB_CHECK_VALID(rb);
    rb_isr_state_t saved;
    RB_ENTER_CRITICAL_FROM_ISR(saved);
    const ret_code_t ret = RB_Commit_Read_Logic(rb, commit, NULL, false);
    RB_EXIT_CRITICAL_FROM_ISR(saved);
    return ret;
}

/**
 * @brief 线程版零拷贝 丢弃指定数组
 * @param rb RB 句柄
 * @param drop 想要丢弃的大小 字节
 * @param dropped 实际丢弃的大小 字节
 * @param isCompatible true 数据＜drop 有多少丢多少 false必须>=drop才能丢弃
 * @return 状态码
 */
ret_code_t RingBuffer_Drop(RingBuffer* rb, uint32_t drop, uint32_t* dropped, bool isCompatible) {
    RB_CHECK_VALID(rb);
    RB_ENTER_CRITICAL();
    const ret_code_t ret = RB_Commit_Read_Logic(rb, drop, dropped, isCompatible);
    RB_EXIT_CRITICAL();
    return ret;
}
/**
 * @brief 中断版零拷贝 丢弃指定数组
 * @param rb RB 句柄
 * @param drop 想要丢弃的大小 字节
 * @param dropped 实际丢弃的大小 字节
 * @param isCompatible true 数据＜drop 有多少丢多少 false必须>=drop才能丢弃
 * @return 状态码
 */
ret_code_t RingBuffer_DropFromISR(RingBuffer* rb, uint32_t drop, uint32_t* dropped,
                                  bool isCompatible) {
    RB_CHECK_VALID(rb);
    rb_isr_state_t saved;
    RB_ENTER_CRITICAL_FROM_ISR(saved);
    const ret_code_t ret = RB_Commit_Read_Logic(rb, drop, dropped, isCompatible);
    RB_EXIT_CRITICAL_FROM_ISR(saved);
    return ret;
}

#endif
