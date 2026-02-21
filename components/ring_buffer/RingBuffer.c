#include "APP_config.h"

#if defined(ENABLE_RINGBUFFER_SYSTEM)
#include <stdbool.h>
#include <string.h>

#include "MemoryAllocation.h"
#include "RingBuffer.h"
#include "barrier.h"
#include "rb_port.h"
/* 状态错误状态码打包宏 */
#define RB_RET(clas_, err_) RET_MAKE(RET_MOD_RB, RET_SUB_RB_CORE, RET_CODE_MAKE((clas_), (err_)))

/* 给 ret_code_t 返回函数用 */
#define RB_CHECK_VALID_RC(rb)                                       \
    do {                                                            \
        if ((rb) == NULL || (rb)->buffer == NULL || (rb)->size < 2) \
            return RB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);      \
    } while (0)

/* 给 uint32_t 返回函数用：非法就返回 0（*/
#define RB_CHECK_VALID_U32(rb)                                                 \
    do {                                                                       \
        if ((rb) == NULL || (rb)->buffer == NULL || (rb)->size < 2) return 0u; \
    } while (0)

/* ret_code_t 版本 */
#define RB_CHECK_ARGS_RC(rb, ptr, size)                                                           \
    do {                                                                                          \
        RB_CHECK_VALID_RC(rb);                                                                    \
        if (!(ptr) || !(size) || *(size) == 0) return RB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG); \
    } while (0)

/**
 * @brief 内部监测RB使用情况
 * @param rb RB句柄
 * @param used 使用的字节数
 * @param remain 剩余字节数
 */
static inline void RB_UpdateWatermark(RingBuffer* rb, uint32_t used, uint32_t remain) {
    rb->last_used   = used;
    rb->last_remain = remain;

    if (used > rb->high_watermark_used) {
        rb->high_watermark_used = used;
    }
}

/**
 * @brief 将数据入队/出队后的位置更新
 * @param rb RB句柄
 * @param current 当前位置
 * @param step 想要入队或者出队的步进数
 * @return 返回新的所引位置
 */
static inline uint32_t RB_NextIndex(const RingBuffer* rb, uint32_t current, uint32_t step) {
    if (rb->isPowerOfTwo_Size) {
        return (current + step) & (rb->size - 1);
    }
    return (current + step) % rb->size;
}

/**
 * @brief 内部辅助函数计算当前RB空间使用了多少字节
 * @param rb RB句柄
 * @return 使用的字节数
 */
static inline uint32_t RB_GetUsed_Core(const RingBuffer* rb) {
    if (rb->isPowerOfTwo_Size) {
        return (rb->rear_index - rb->front_index + rb->size) & (rb->size - 1);
    }
    return (rb->rear_index - rb->front_index + rb->size) % rb->size;
}

/**
 * @brief 计算内部可使用的字节数
 * @param rb RB句柄
 * @return 内部可使用的字节数
 */
static inline uint32_t RB_GetRemain_Core(const RingBuffer* rb) {
    return rb->size - RB_GetUsed_Core(rb) - 1;
}

/**
 * @brief 内部写
 * @param rb RB句柄
 * @param add 数据源
 * @param size 输入： 需要写多少字节数 输出：实际上写入的字节数
 * @param isForce true 有多少空间写多少 false 必须能全部写入才写入
 * @param isSPSC true SPSC模式 内部函数使用内存屏障保护 外部函数不能使用临界区 false 普通RB
 * 需要外部函数加临界区
 * @return 状态码
 */
static ret_code_t RB_Write_Logic(RingBuffer* rb, const uint8_t* add, uint32_t* size, bool isForce,
                                 bool isSPSC) {
    if (isSPSC) {
        compiler_barrier();
        mem_barrier();
    }
    const uint32_t remain = RB_GetRemain_Core(rb);

    if (remain < *size) {
        if (isForce)
            *size = remain;
        else {
            rb->overflow_cnt++;
            return RB_RET(RET_CLASS_RESOURCE, RET_R_NO_MEM);
        }
    }
    if (*size == 0) return RET_OK;

    const uint32_t end_size = rb->size - rb->rear_index;
    if (end_size >= *size) {
        memcpy(rb->buffer + rb->rear_index, add, *size);
    } else {
        memcpy(rb->buffer + rb->rear_index, add, end_size);
        memcpy(rb->buffer, add + end_size, *size - end_size);
    }
    /* SPSC 模式 */
    if (isSPSC) {
        compiler_barrier();
        mem_barrier();
    }
    rb->rear_index = RB_NextIndex(rb, rb->rear_index, *size);
    /* 成功写入后：更新水位线 */
    {
        const uint32_t used2 = RB_GetUsed_Core(rb);
        const uint32_t rem2  = rb->size - used2 - 1;
        RB_UpdateWatermark(rb, used2, rem2);
    }

    return RET_OK;
}

/**
 * @brief 内部读取数据
 * @param rb RB句柄
 * @param add 数据存储地址
 * @param size 输入： 需要写多少字节数 输出：实际上写入的字节数
 * @param isForce true 有多少读多少 false 在有想要的数据大小数才读取
 * @param isPeek  true 读取后但是不消耗数据 false 读取后消耗数据
 * @param isSPSC  true SPSC模式 内部函数使用内存屏障保护 外部函数不能使用临界区 false 普通RB
 * 需要外部函数加临界区
 * @return 状态码
 */
static ret_code_t RB_Read_Logic(RingBuffer* rb, uint8_t* add, uint32_t* size, bool isForce,
                                bool isPeek, bool isSPSC) {
    const uint32_t used = RB_GetUsed_Core(rb);
    /* SPSC 模式 */
    if (isSPSC) {
        compiler_barrier();
        mem_barrier();
    }
    if (used < *size) {
        if (isForce)
            *size = used;
        else {
            rb->underflow_cnt++;
            return RB_RET(RET_CLASS_DATA, RET_R_DATA_NOT_ENOUGH);
        }
    }
    if (*size == 0) return RET_OK;

    const uint32_t end_size = rb->size - rb->front_index;
    if (end_size >= *size) {
        memcpy(add, rb->buffer + rb->front_index, *size);
    } else {
        memcpy(add, rb->buffer + rb->front_index, end_size);
        memcpy(add + end_size, rb->buffer, *size - end_size);
    }
    if (isSPSC && !isPeek) {
        compiler_barrier();
        mem_barrier();
    }

    if (!isPeek) {
        rb->front_index = RB_NextIndex(rb, rb->front_index, *size);
    }
    {
        const uint32_t used2 = RB_GetUsed_Core(rb);
        const uint32_t rem2  = rb->size - used2 - 1;
        RB_UpdateWatermark(rb, used2, rem2);
    }
    return RET_OK;
}

/**
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
/**============================================================================================ */
/**==================================      Span拷贝工具     ===================================== */
/**============================================================================================ */
/**
 * @brief 将线性缓冲区数据写入 span 描述的两段目标空间
 * @param span 由 Reserve 接口返回的目标窗口（p1/n1 + p2/n2）
 * @param src  线性源数据地址
 * @param len  想要拷贝的字节数
 * @note 实际写入字节数 = min(len, span->n1 + span->n2)
 */
void RingBuffer_SpanWriteFromLinear(const RingBufferSpan* span, const uint8_t* src, uint32_t len) {
    if (!span || !src || len == 0u) return;

    uint32_t copied = 0u;
    if (span->n1 > 0u) {
        const uint32_t n = (len < span->n1) ? len : span->n1;
        memcpy(span->p1, src, n);
        copied = n;
    }

    if (copied < len && span->n2 > 0u) {
        uint32_t n = len - copied;
        if (n > span->n2) n = span->n2;
        memcpy(span->p2, src + copied, n);
    }
}

/**
 * @brief 将 span 描述的两段数据合并读取到线性目标缓冲区
 * @param span 数据窗口（通常由 ReadReserve 返回）
 * @param dst  线性目标地址
 * @param len  想要读取的字节数
 * @note 实际读取字节数 = min(len, span->n1 + span->n2)
 */
void RingBuffer_SpanReadToLinear(const RingBufferSpan* span, uint8_t* dst, uint32_t len) {
    if (!span || !dst || len == 0u) return;

    uint32_t copied = 0u;
    if (span->n1 > 0u) {
        const uint32_t n = (len < span->n1) ? len : span->n1;
        memcpy(dst, span->p1, n);
        copied = n;
    }

    if (copied < len && span->n2 > 0u) {
        uint32_t n = len - copied;
        if (n > span->n2) n = span->n2;
        memcpy(dst + copied, span->p2, n);
    }
}

/**
 * @brief 从环形源缓冲区拷贝数据到 span 目标窗口（支持源端回绕）
 * @param span         目标窗口（通常由 WriteReserve 返回）
 * @param src_ring     环形源缓冲区首地址（如 DMA circular buffer）
 * @param src_ring_len 环形源缓冲区长度（字节）
 * @param src_pos      源端起始索引
 * @param len          想要拷贝的字节数
 * @note 实际写入字节数 = min(len, span->n1 + span->n2)
 */
void RingBuffer_SpanWriteFromCircular(const RingBufferSpan* span, const uint8_t* src_ring,
                                      uint32_t src_ring_len, uint32_t src_pos, uint32_t len) {
    if (!span || !src_ring || src_ring_len == 0u || len == 0u) return;

    /* 规范化源端起点，避免 src_pos 超过环长 */
    uint32_t pos = src_pos % src_ring_len;

    /* 先写入目标第一段 p1 */
    if (span->n1 > 0u) {
        const uint32_t n1 = (len < span->n1) ? len : span->n1;
        if (n1 > 0u) {
            /* 源端从 pos 到环尾的连续可读长度 */
            const uint32_t tail = src_ring_len - pos;
            if (n1 <= tail) {
                /* 源端不回绕：一次 memcpy 完成 */
                memcpy(span->p1, &src_ring[pos], n1);
                pos += n1;
                /* 命中环尾时，回到 0 */
                if (pos == src_ring_len) pos = 0u;
            } else {
                /* 源端回绕：先拷尾部，再拷头部 */
                memcpy(span->p1, &src_ring[pos], tail);
                memcpy(span->p1 + tail, &src_ring[0], n1 - tail);
                pos = n1 - tail;
            }
        }
    }

    /* 再写入目标第二段 p2（仅当 len 超过了 p1 已承担部分） */
    if (span->n2 > 0u && len > span->n1) {
        uint32_t n2 = len - span->n1;
        if (n2 > span->n2) n2 = span->n2;
        if (n2 > 0u) {
            /* 重新计算当前 pos 到环尾的连续长度 */
            const uint32_t tail = src_ring_len - pos;
            if (n2 <= tail) {
                /* 源端不回绕 */
                memcpy(span->p2, &src_ring[pos], n2);
            } else {
                /* 源端回绕 */
                memcpy(span->p2, &src_ring[pos], tail);
                memcpy(span->p2 + tail, &src_ring[0], n2 - tail);
            }
        }
    }
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
    rb->high_watermark_used          = 0;
    rb->overflow_cnt                 = 0;
    rb->underflow_cnt                = 0;
    rb->last_remain                  = 0;
    rb->last_used                    = 0;
    return RET_OK;
}

/**
 * @brief 线程版获取使用的容量字节数
 * @param rb RB句柄
 * @return 已经使用的容量字节数
 */
uint32_t RingBuffer_GetUsedSize(const RingBuffer* rb) {
    RB_CHECK_VALID_U32(rb);
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
    RB_CHECK_VALID_U32(rb);
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
    RB_CHECK_VALID_U32(rb);
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
    RB_CHECK_VALID_U32(rb);
    rb_isr_state_t saved;
    RB_ENTER_CRITICAL_FROM_ISR(saved);
    const uint32_t ret = RB_GetRemain_Core(rb);
    RB_EXIT_CRITICAL_FROM_ISR(saved);
    return ret;
}

/**
 * @brief 线程版 写入数据到 RB
 * @param rb RB句柄
 * @param add 数据源
 * @param size 输入： 需要写多少字节数 输出：实际上写入的字节数
 * @param isForceWrite true 有多少空间就写多少数据 false 必须全部能够装下才能写入
 * @return 32位分段状态码
 */
ret_code_t WriteRingBuffer(RingBuffer* rb, const uint8_t* add, uint32_t* size,
                           const uint8_t isForceWrite) {
    RB_CHECK_ARGS_RC(rb, add, size);
    RB_ENTER_CRITICAL();
    const ret_code_t ret = RB_Write_Logic(rb, add, size, isForceWrite, false);
    RB_EXIT_CRITICAL();
    return ret;
}
/**
 * @brief 中断版 写入数据到 RB
 * @param rb RB句柄
 * @param add 数据源
 * @param size 输入： 需要写多少字节数 输出：实际上写入的字节数
 * @param isForceWrite true 有多少空间就写多少数据 false 必须全部能够装下才能写入
 * @return 32位分段状态码
 */
ret_code_t WriteRingBufferFromISR(RingBuffer* rb, const uint8_t* add, uint32_t* size,
                                  const uint8_t isForceWrite) {
    RB_CHECK_ARGS_RC(rb, add, size);
    rb_isr_state_t saved;
    RB_ENTER_CRITICAL_FROM_ISR(saved);
    const ret_code_t ret = RB_Write_Logic(rb, add, size, isForceWrite, false);
    RB_EXIT_CRITICAL_FROM_ISR(saved);
    return ret;
}

/**
 * @brief 线程版 读取 RB 数据
 * @param rb RB句柄
 * @param add 数据输出储存地址
 * @param size 输入： 需要读多少字节数 输出：实际上读的字节数
 * @param isForceRead true 在数据不够时有多少读多少 false 必须有足够数据才读取
 * @return 32位分段状态码
 */
ret_code_t ReadRingBuffer(RingBuffer* rb, uint8_t* add, uint32_t* size, const uint8_t isForceRead) {
    RB_CHECK_ARGS_RC(rb, add, size);
    RB_ENTER_CRITICAL();
    const ret_code_t ret = RB_Read_Logic(rb, add, size, isForceRead, false, false);
    RB_EXIT_CRITICAL();
    return ret;
}
/**
 * @brief 中断版 读取 RB 数据
 * @param rb RB句柄
 * @param add 数据输出存储地址
 * @param size 输入： 需要读多少字节数 输出：实际上读的字节数
 * @param isForceRead true 在数据不够时有多少读多少 false 必须有足够数据才读取
 * @return 32位分段状态码
 */
ret_code_t ReadRingBufferFromISR(RingBuffer* rb, uint8_t* add, uint32_t* size,
                                 const uint8_t isForceRead) {
    RB_CHECK_ARGS_RC(rb, add, size);
    rb_isr_state_t saved;
    RB_ENTER_CRITICAL_FROM_ISR(saved);
    const ret_code_t ret = RB_Read_Logic(rb, add, size, isForceRead, false, false);
    RB_EXIT_CRITICAL_FROM_ISR(saved);
    return ret;
}

/**
 * @brief 线程版 窥视 RB 数据 但是不消耗数据
 * @param rb RB句柄
 * @param add 数据输出存储地址
 * @param size 输入： 需要读多少字节数 输出：实际上读的字节数
 * @param isForcePeek true 在数据不够时有多少读多少 false 必须有足够数据才读取
 * @return 32位分段状态码
 */
ret_code_t PeekRingBuffer(const RingBuffer* rb, uint8_t* add, uint32_t* size,
                          const uint8_t isForcePeek) {
    RB_CHECK_ARGS_RC(rb, add, size);
    RingBuffer* rb_mut = (RingBuffer*)rb;
    RB_ENTER_CRITICAL();
    const ret_code_t ret = RB_Read_Logic(rb_mut, add, size, isForcePeek, true, false);
    RB_EXIT_CRITICAL();
    return ret;
}

/**
 * @beief 线程版 重置 RB 空间
 * @param rb RB句柄
 * @return 状体码
 */
ret_code_t ResetRingBuffer(RingBuffer* rb) {
    RB_CHECK_VALID_RC(rb);
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
    RB_CHECK_VALID_RC(rb);
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
    /* 成功写入后：更新水位线 */
    {
        const uint32_t used2 = RB_GetUsed_Core(rb);
        const uint32_t rem2  = rb->size - used2 - 1;
        RB_UpdateWatermark(rb, used2, rem2);
    }

    return RET_OK;
}
/**
 * @brief 线程版 提交实际写入的大小
 * @param rb RB 句柄
 * @param commit 实际写入的大小
 * @return 状态码
 */
ret_code_t RingBuffer_WriteCommit(RingBuffer* rb, uint32_t commit) {
    RB_CHECK_VALID_RC(rb);
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
    RB_CHECK_VALID_RC(rb);
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
 * @param granted 实际批准的大小
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
 * @param granted 实际批准的大小
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
        /* 成功读取：更新水位线 */
        {
            const uint32_t used2 = RB_GetUsed_Core(rb);
            const uint32_t rem2  = rb->size - used2 - 1;
            RB_UpdateWatermark(rb, used2, rem2);
        }
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
    RB_CHECK_VALID_RC(rb);
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
    RB_CHECK_VALID_RC(rb);
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
    RB_CHECK_VALID_RC(rb);
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
    RB_CHECK_VALID_RC(rb);
    rb_isr_state_t saved;
    RB_ENTER_CRITICAL_FROM_ISR(saved);
    const ret_code_t ret = RB_Commit_Read_Logic(rb, drop, dropped, isCompatible);
    RB_EXIT_CRITICAL_FROM_ISR(saved);
    return ret;
}
/**============================================================================================ */
/**==================================       SPSC          ===================================== */
/**============================================================================================ */
/* */
/**
 * @brief SPSC版 写入数据到 RB
 * @param rb RB句柄
 * @param add 数据源
 * @param size 输入： 需要写多少字节数 输出：实际上写入的字节数
 * @param isForceWrite true 有多少空间就写多少数据 false 必须全部能够装下才能写入
 * @return 32位分段状态码
 */
ret_code_t WriteRingBuffer_SPSC(RingBuffer* rb, const uint8_t* add, uint32_t* size,
                                uint8_t isForceWrite) {
    RB_CHECK_ARGS_RC(rb, add, size);
    return RB_Write_Logic(rb, add, size, isForceWrite, true);
}
/**
 * @brief SPSC版 从RB 读取数据到 add
 * @param rb RB句柄
 * @param add 数据存储地址
 * @param size 输入： 需要读多少字节数 输出：实际上读的字节数
 * @param isForceRead  true 有多少空间就写多少数据 false 必须全部能够装下才能写入
 * @return 32位分段状态码
 */
ret_code_t ReadRingBuffer_SPSC(RingBuffer* rb, uint8_t* add, uint32_t* size, uint8_t isForceRead) {
    RB_CHECK_ARGS_RC(rb, add, size);
    return RB_Read_Logic(rb, add, size, isForceRead, false, true);
}
/**
 * @brief SPSC版 从RB 读取数据到 add
 * @param rb RB句柄
 * @param add 数据存储地址
 * @param size 输入： 需要读多少字节数 输出：实际上读的字节数
 * @param isForcePeek  true 有多少空间就写多少数据 false 必须全部能够装下才能写入
 * @return 32位分段状态码
 */
ret_code_t PeekRingBuffer_SPSC(const RingBuffer* rb, uint8_t* add, uint32_t* size,
                               uint8_t isForcePeek) {
    RB_CHECK_ARGS_RC(rb, add, size);
    return RB_Read_Logic((RingBuffer*)rb, add, size, isForcePeek, true, true);
}
/**
 * @brief 获取所需空间大小的 相干信息
 * @param rb RB句柄
 * @param want 想要写入的数据空间大小 字节
 * @param out  返回的空间信息
 * @param granted 实际返回的空间大小
 * @param isCompatible true 空间不足返回全部 false 空间不足返回 0
 * @return 32位分段状态码
 */
ret_code_t RingBuffer_WriteReserve_SPSC(RingBuffer* rb, uint32_t want, RingBufferSpan* out,
                                        uint32_t* granted, bool isCompatible) {
    if (!rb || !out || !granted || !rb->buffer || rb->size < 2)
        return RB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    compiler_barrier();
    mem_barrier();  // 对于跨核通信，读取对方更新的索引前需要DMB
    return RB_Reserve_Logic(rb, want, out, granted, isCompatible, true);
}

/**
 * @brief 将写入的字节数 索引移动
 * @param rb RB 句柄
 * @param commit 需要提交的字节数
 * @return 32位状态码
 */
ret_code_t RingBuffer_WriteCommit_SPSC(RingBuffer* rb, uint32_t commit) {
    compiler_barrier();
    mem_barrier();
    const uint32_t remain = RB_GetRemain_Core(rb);
    if (commit > remain) return RB_RET(RET_CLASS_RESOURCE, RET_R_NO_MEM);
    rb->rear_index = RB_NextIndex(rb, rb->rear_index, commit);
    /* 成功写入后：更新水位线 */
    {
        const uint32_t used2 = RB_GetUsed_Core(rb);
        const uint32_t rem2  = rb->size - used2 - 1;
        RB_UpdateWatermark(rb, used2, rem2);
    }

    return RET_OK;
}

/**
 * @brief 获取所需空间大小的 相干信息
 * @param rb RB句柄
 * @param want 想要读取的数据空间大小 字节
 * @param out  返回的空间信息
 * @param granted 实际返回的空间大小
 * @param isCompatible true 空间不足返回全部 false 空间不足返回 0
 * @return 32位分段状态码
 */
ret_code_t RingBuffer_ReadReserve_SPSC(RingBuffer* rb, uint32_t want, RingBufferSpan* out,
                                       uint32_t* granted, bool isCompatible) {
    if (!rb || !out || !granted || !rb->buffer || rb->size < 2)
        return RB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    compiler_barrier();
    mem_barrier();
    return RB_Reserve_Logic(rb, want, out, granted, isCompatible, false);
}

/**
 * @brief 将读取的字节数 索引移动
 * @param rb RB 句柄
 * @param commit 需要提交的字节数
 * @return 32位状态码
 */
ret_code_t RingBuffer_ReadCommit_SPSC(RingBuffer* rb, uint32_t commit) {
    compiler_barrier();
    mem_barrier();
    const uint32_t used = RB_GetUsed_Core(rb);
    if (commit > used) return RB_RET(RET_CLASS_DATA, RET_R_DATA_NOT_ENOUGH);
    rb->front_index = RB_NextIndex(rb, rb->front_index, commit);
    /* 成功读取：更新水位线 */
    {
        const uint32_t used2 = RB_GetUsed_Core(rb);
        const uint32_t rem2  = rb->size - used2 - 1;
        RB_UpdateWatermark(rb, used2, rem2);
    }
    return RET_OK;
}

/**============================================================================================ */
/**==================================       状态监测       ===================================== */
/**============================================================================================ */

/* */
/**
 * @brief 判断RB是否为空
 * @param rb RB句柄
 * @return
 */
bool RingBuffer_IsEmpty(const RingBuffer* rb) {
    return (RingBuffer_GetUsedSize(rb) == 0);
}
/**
 * @brief 判断RB是否已满
 * @param rb RB句柄
 * @return
 */
bool RingBuffer_IsFull(const RingBuffer* rb) {
    return RingBuffer_GetRemainSize(rb) == 0;
}
/**
 * @brief 当前的空间占用情况 线程版
 * @param rb RB 句柄
 * @return 当前的空间占用千分比
 */
uint16_t RingBuffer_GetUsedPermille(const RingBuffer* rb) {
    RB_CHECK_VALID_U32(rb);
    const uint32_t used = RingBuffer_GetUsedSize(rb);
    const uint32_t cap  = (rb->size > 1u) ? (rb->size - 1u) : 1u;
    uint32_t p          = (used * 1000u) / cap;
    if (p > 1000u) p = 1000u;
    return (uint16_t)p;
}
/**
 * @brief 当前的空间占用情况 终端的版
 * @param rb RB 句柄
 * @return 当前的空间占用千分比
 */
uint16_t RingBuffer_GetUsedPermilleFromISR(const RingBuffer* rb) {
    RB_CHECK_VALID_U32(rb);
    const uint32_t used = RingBuffer_GetUsedSizeFromISR(rb);
    const uint32_t cap  = (rb->size > 1u) ? (rb->size - 1u) : 1u;
    uint32_t p          = (used * 1000u) / cap;
    if (p > 1000u) p = 1000u;
    return (uint16_t)p;
}
/**
 * @brief 已用空间是否大于特点阈值
 * @param rb RB 句柄
 * @param used_th 阈值
 * @return 已用空间是否大于阈值
 */
bool RingBuffer_UsedAtLeast(const RingBuffer* rb, uint32_t used_th) {
    return RingBuffer_GetUsedSize(rb) >= used_th;
}
/**
 * @brief 剩余空间是否大于特点阈值
 * @param rb RB 句柄
 * @param remain_th 阈值
 * @return 剩余空间是否大于阈值
 */
bool RingBuffer_RemainAtMost(const RingBuffer* rb, uint32_t remain_th) {
    return RingBuffer_GetRemainSize(rb) <= remain_th;
}
/**
 * @brief 获取当前连续可读的字节数（不需要回绕的部分）
 * @param rb RB句柄
 * @return 连续可读字节数
 */
uint32_t RingBuffer_GetContigRead(const RingBuffer* rb) {
    if (!rb || !rb->buffer || rb->size < 2) return 0;

    // 逻辑复用 RB_Reserve_Logic 的读部分计算 n1
    // 读模式: front ... rear (或 end)

    uint32_t n1          = 0;
    const uint32_t front = rb->front_index;
    const uint32_t rear  = rb->rear_index;

    if (front <= rear) {
        // 连续数据：front ... rear
        n1 = rear - front;
    } else {
        // 跨尾数据：front ... end
        n1 = rb->size - front;
    }

    return n1;
}

/**
 * @brief 获取当前连续可写的字节数（不需要回绕的部分）
 * @param rb RB句柄
 * @return 连续可写字节数
 */
uint32_t RingBuffer_GetContigWrite(const RingBuffer* rb) {
    if (!rb || !rb->buffer || rb->size < 2) return 0;
    // 逻辑复用 RB_Reserve_Logic 的写部分计算 n1
    // 写模式: rear ... front-1 (或 end)
    uint32_t n1          = 0;
    const uint32_t front = rb->front_index;
    const uint32_t rear  = rb->rear_index;
    const uint32_t size  = rb->size;

    if (rear < front) {
        // 中间空闲：rear ... front-1
        n1 = front - rear - 1;
    } else {
        // 两头空闲：rear ... end
        n1 = size - rear;
        if (front == 0) {
            n1--;  // 如果front在0，rear不能写到最后的一个字节，必须保留一个空位
        }
    }

    return n1;
}

/**
 * @brief 获取指定RB的运行状态快照
 * @param rb RB 句柄
 * @return
 */
RingBufferStatus RingBuffer_GetStatus(const RingBuffer* rb) {
    RB_ENTER_CRITICAL();
    const RingBufferStatus status = {
        .used                = RingBuffer_GetUsedSize(rb),
        .remain              = RingBuffer_GetRemainSize(rb),
        .size                = rb->size,
        .empty               = RingBuffer_IsEmpty(rb),
        .full                = RingBuffer_IsFull(rb),
        .contig_read         = RingBuffer_GetContigRead(rb),
        .contig_write        = RingBuffer_GetContigWrite(rb),
        .high_watermark_used = rb->high_watermark_used,
        .overflow_cnt        = rb->overflow_cnt,
        .underflow_cnt       = rb->underflow_cnt,
        .last_remain         = rb->last_remain,
        .last_used           = rb->last_used,
    };
    RB_EXIT_CRITICAL();
    return status;
}
static inline uint32_t rb_mod(uint32_t x, uint32_t m) {
    return (m == 0u) ? 0u : (x % m);
}

bool RingBuffer_SPSC_OverwriteIfExists(RingBuffer* rb, const uint8_t* item, uint32_t item_size,
                                       uint32_t event_id_off, uint32_t key_off) {
    if (!rb || !item || item_size == 0u) return false;
    if (event_id_off + 4u > item_size) return false;
    if (key_off + 4u > item_size) return false;

    uint32_t target_event_id, target_key;
    memcpy(&target_event_id, item + event_id_off, 4u);
    memcpy(&target_key, item + key_off, 4u);

    uint32_t pm;
    OSAL_enter_critical_ex(&pm);

    const uint32_t used = RingBuffer_GetUsedSize(rb);
    const uint32_t cnt  = used / item_size;
    if (cnt == 0u) {
        OSAL_exit_critical_ex(pm);
        return false;
    }

    for (uint32_t i = 0; i < cnt; i++) {
        const uint32_t pos = rb_mod(rb->front_index + i * item_size, rb->size);

        uint8_t tmp[8];
        for (uint32_t k = 0; k < 4u; k++) {
            tmp[k] = rb->buffer[rb_mod(pos + event_id_off + k, rb->size)];
        }
        for (uint32_t k = 0; k < 4u; k++) {
            tmp[4u + k] = rb->buffer[rb_mod(pos + key_off + k, rb->size)];
        }

        uint32_t cur_event_id, cur_key;
        memcpy(&cur_event_id, tmp, 4u);
        memcpy(&cur_key, tmp + 4u, 4u);

        if (cur_event_id == target_event_id && cur_key == target_key) {
            const uint32_t tail  = rb->size - pos;
            const uint32_t first = (item_size <= tail) ? item_size : tail;
            memcpy(rb->buffer + pos, item, first);
            if (first < item_size) {
                memcpy(rb->buffer, item + first, item_size - first);
            }
            OSAL_exit_critical_ex(pm);
            return true;
        }
    }

    OSAL_exit_critical_ex(pm);
    return false;
}
#endif
