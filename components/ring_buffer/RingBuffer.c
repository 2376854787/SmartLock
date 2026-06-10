#include "APP_config.h"

#if (defined(CFG_FEAT_RINGBUFFER_SYSTEM) && (CFG_FEAT_RINGBUFFER_SYSTEM == 1))
#include <stdbool.h>
#include <string.h>

#include "MemoryAllocation.h"
#include "RingBuffer.h"
#include "assert_cus.h"
#include "barrier.h"
#include "rb_port.h"

/* ============================================================================
 * 索引模型（重要）：字节流环，rear_index/front_index 是「自由递增」的写/读字节
 * 序号（和 DPDK rte_ring 的 head/tail 同构），不在 [0,size) 内回绕，而是用 uint32
 * 一直增长、自然在 UINT32_MAX 处回绕。
 *   - 定位物理槽：index & mask（mask = size-1）。
 *   - 已用字节数：used = rear_index - front_index（无符号减法，rear 绕过 0 也对）。
 *   - 剩余可写：  remain = size - used（去哨兵，整段容量都可用，无需留 1 字节）。
 *   - 判满/判空： full = (used == size)；empty = (used == 0)。
 *
 * 为什么必须 2 的幂：自由递增 + &mask 只在 size 是 2 的幂时正确。序号在 UINT32_MAX
 * 处回绕，只有 size 是 2 的幂、mask=size-1 时，(index & mask) 才能在回绕点保持物理
 * 槽连续；非 2 的幂会在回绕处错位。故 CreateRingBuffer 强制把请求 size 向上取整到
 * 2 的幂（如 1000→1024）再分配，取整后的容量可全部使用。
 * ============================================================================
 *
 * 并发模型约定（重要，违反会丢数据/损坏索引）：
 *
 * 同一个 RingBuffer 实例必须固定使用以下三种模型之一，禁止混用：
 *
 *   A. 带锁线程态/ISR 互斥模型：
 *      只调用 WriteRingBuffer / ReadRingBuffer / WriteRingBufferFromISR /
 *      ReadRingBufferFromISR / WriteReserve(...) + WriteCommit(...) / Drop(...)。
 *      所有接口内部用 RB_ENTER_CRITICAL / FROM_ISR 互斥。
 *
 *   B. SPSC 无锁模型（单生产者 + 单消费者）：
 *      生产者只调 WriteRingBuffer_SPSC / WriteReserve_SPSC + WriteCommit_SPSC，
 *      消费者只调 ReadRingBuffer_SPSC / PeekRingBuffer_SPSC /
 *      ReadReserve_SPSC + ReadCommit_SPSC。
 *      内部用内存屏障，不取临界区。
 *      禁止把 B 模型接口与 A 模型接口混用到同一个 RB（混用时 SPSC 侧无锁，
 *      看不到 A 模型在临界区内的中间状态，会数据竞争）。
 *
 *   C. 零拷贝单生产者 Reserve/Commit：
 *      Reserve/Commit 之间“窗口”由调用方持有，因此同一侧（写或读）只能
 *      有一个并发者。
 *
 * 统计字段（high_watermark_used / overflow_cnt / underflow_cnt / last_used /
 * last_remain）非原子：
 *   - 模型 A 由临界区保护，结果准确。
 *   - 模型 B（SPSC）下，水位线在写者侧更新（写者最有可能让 used 增大）；
 *     消费者侧不再更新水位线，避免与生产者 RMW 撞车。
 *     这意味着 SPSC 模型下 high_watermark_used 是“写者刚提交后的快照”，
 *     不是“瞬时绝对最大值”——这对统计语义已经足够。
 * ========================================================================= */

/* 状态错误状态码打包宏 */
#define RB_RET(clas_, err_) RET_MAKE(RET_MOD_RB, RET_SUB_RB_CORE, RET_CODE_MAKE((clas_), (err_)))

/* 给 ret_code_t 返回函数用 */
#define RB_CHECK_VALID_RC(rb)                                                         \
    do {                                                                              \
        ASSERT_PARAM(((rb) != NULL) && ((rb)->buffer != NULL) && ((rb)->size >= 2u)); \
        if ((rb) == NULL || (rb)->buffer == NULL || (rb)->size < 2)                   \
            return RB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);                        \
    } while (0)

/* 给 uint32_t 返回函数用：非法就返回 0 */
#define RB_CHECK_VALID_U32(rb)                                                        \
    do {                                                                              \
        ASSERT_PARAM(((rb) != NULL) && ((rb)->buffer != NULL) && ((rb)->size >= 2u)); \
        if ((rb) == NULL || (rb)->buffer == NULL || (rb)->size < 2) return 0u;        \
    } while (0)

/* ret_code_t 版本 */
#define RB_CHECK_ARGS_RC(rb, ptr, size)                                                           \
    do {                                                                                          \
        RB_CHECK_VALID_RC(rb);                                                                    \
        ASSERT_PARAM(((ptr) != NULL) && ((size) != NULL) && (*(size) != 0u));                     \
        if (!(ptr) || !(size) || *(size) == 0) return RB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG); \
    } while (0)

/* 前向声明：RB_UpdateWatermark 在统计开启时要重算 used，而 RB_GetUsed_Core
 * 定义在后面。 */
static inline uint32_t RB_GetUsed_Core(const RingBuffer* rb);

/* rb_sync_t（RB_SYNC_SMP / RB_SYNC_DMA）是公开类型，定义在 RingBuffer.h。
 * 带锁模型内部还需要一个「不发任何屏障」的取值，用 0 表示，仅在本文件内使用，
 * 不暴露给调用方（SPSC 接口只接受 SMP / DMA）。 */
#define RB_SYNC_NONE ((rb_sync_t)0)

/* 按 sync 种类发屏障。RB_SYNC_NONE 完全不展开任何指令。 */
static inline void RB_Barrier(rb_sync_t sync) {
    if (sync == RB_SYNC_SMP) {
        smp_mem_barrier();
    } else if (sync == RB_SYNC_DMA) {
        dma_mem_barrier();
    }
}

/**
 * @brief 内部更新水位线/最近 used/remain。
 *        模型 A（带锁）：调用方在临界区内调用，原子。
 *        模型 B（SPSC）：仅写者侧调用，单写者保证 RMW 安全。
 */
static inline void RB_UpdateWatermark(RingBuffer* rb) {
#if RB_ENABLE_STATS
    /* used/remain 在函数内部重算：统计关闭时整段消失，调用点无需任何 #if，
     * 也不会在热路径上留下「算了又不用」的死代码。
     * 去哨兵后 remain = size - used（不再 -1），容量可全用。 */
    const uint32_t used   = RB_GetUsed_Core(rb);
    const uint32_t remain = rb->size - used;

    rb->last_used         = used;
    rb->last_remain       = remain;

    if (used > rb->high_watermark_used) {
        rb->high_watermark_used = used;
    }
#else
    (void)rb; /* 统计关闭：空操作 */
#endif
}

/**
 * @brief 把读/写序号推进 step 个字节
 * @param rb RB句柄（仅为保持接口，本实现不需要它）
 * @param current 当前序号
 * @param step 想要入队或者出队的步进数
 * @return 推进后的新序号
 * @note 序号「自由递增」，不再回绕到 [0,size)：直接 current + step，让 uint32
 *       自然在 UINT32_MAX 处回绕。定位物理槽由调用方各自 & mask 完成。
 *       这要求 size 是 2 的幂（见 RB_GetUsed_Core 注释），由 Create 保证。
 */
static inline uint32_t RB_NextIndex(const RingBuffer* rb, uint32_t current, uint32_t step) {
    (void)rb;
    return current + step;
}

/**
 * @brief 内部辅助函数计算当前RB空间使用了多少字节
 * @param rb RB句柄
 * @return 使用的字节数
 * @note 序号自由递增下，used = rear - front 的无符号减法天然正确：即使 rear 已
 *       绕过 UINT32_MAX 而 front 没绕，二者的无符号差仍是真实的已用字节数。这只
 *       在 size 是 2 的幂时与 & mask 定位自洽——非 2 的幂时回绕点会让物理槽错位，
 *       所以 Create 强制取整到 2 的幂。used 永不超过 size（写入前已检查 remain）。
 */
static inline uint32_t RB_GetUsed_Core(const RingBuffer* rb) {
    /* 用本地变量复制，避免两次读 rear/front 之间的不一致被外面看见 */
    const uint32_t rear  = rb->rear_index;
    const uint32_t front = rb->front_index;
    return rear - front;
}

/**
 * @brief 计算内部可使用的字节数
 * @param rb RB句柄
 * @return 内部可使用的字节数
 * @note 去哨兵后 remain = size - used（不再 -1），整段容量都可写满。
 */
static inline uint32_t RB_GetRemain_Core(const RingBuffer* rb) {
    return rb->size - RB_GetUsed_Core(rb);
}

/**
 * @brief 内部写
 * @param rb RB句柄
 * @param add 数据源
 * @param size 输入： 需要写多少字节数 输出：实际上写入的字节数
 * @param isForce true 有多少空间写多少 false 必须能全部写入才写入
 * @param sync   屏障种类：RB_SYNC_NONE（带锁，外部已互斥）/ RB_SYNC_SMP（SPSC 对端
 *               是软件执行流）/ RB_SYNC_DMA（SPSC 对端是 DMA/外设）。非 NONE 时外部
 *               不能再加临界区。
 * @return 状态码
 */
static ret_code_t RB_Write_Logic(RingBuffer* rb, const uint8_t* add, uint32_t* size, bool isForce,
                                 rb_sync_t sync) {
    /* 生产者侧读 front（经 GetRemain）不需要前置屏障：
     *   1) front 读到旧值只会让 remain 偏小——少写，保守安全；
     *   2) 后面对数据区的 store 不会被硬件投机提交到这次 load 之前
     *      （store 只在指令流非投机后才进入内存，CPU 不投机提交写）。
     * 与 Linux kfifo 的 __kfifo_in 一致：生产者侧无 acquire 屏障。 */
    const uint32_t remain = RB_GetRemain_Core(rb);

    if (remain < *size) {
        if (isForce) {
            *size = remain;
        } else {
#if RB_ENABLE_STATS
            rb->overflow_cnt++;
#endif
            return RB_RET(RET_CLASS_RESOURCE, RET_R_NO_MEM);
        }
    }
    if (*size == 0) return RET_OK;

    /* rear_index 是自由递增序号，先 & mask 取物理槽下标；end_size 是从该槽到物理
     * 尾部的连续距离。第一段写 min(want, end_size)，剩余从物理 0 处续写。 */
    const uint32_t wpos     = rb->rear_index & rb->mask;
    const uint32_t end_size = rb->size - wpos;
    if (end_size >= *size) {
        memcpy(rb->buffer + wpos, add, *size);
    } else {
        memcpy(rb->buffer + wpos, add, end_size);
        memcpy(rb->buffer, add + end_size, *size - end_size);
    }
    /* SPSC release：保证数据写完才更新 rear_index，否则消费者可能看到
     * 新的 rear_index 但旧的数据。 */
    RB_Barrier(sync);
    rb->rear_index = RB_NextIndex(rb, rb->rear_index, *size);
    /* 成功写入后：更新水位线（SPSC 下也仅在写者侧更新，安全）。 */
    RB_UpdateWatermark(rb);

    return RET_OK;
}

/**
 * @brief 内部读取数据
 * @param rb RB句柄
 * @param add 数据存储地址
 * @param size 输入： 需要写多少字节数 输出：实际上写入的字节数
 * @param isForce true 有多少读多少 false 在有想要的数据大小数才读取
 * @param isPeek  true 读取后但是不消耗数据 false 读取后消耗数据
 * @param sync   屏障种类：RB_SYNC_NONE（带锁，外部已互斥）/ RB_SYNC_SMP（SPSC 对端
 *               是软件执行流）/ RB_SYNC_DMA（SPSC 对端是 DMA/外设）。非 NONE 时外部
 *               不能再加临界区。
 * @return 状态码
 */
static ret_code_t RB_Read_Logic(RingBuffer* rb, uint8_t* add, uint32_t* size, bool isForce,
                                bool isPeek, rb_sync_t sync) {
    const uint32_t used = RB_GetUsed_Core(rb);
    /* SPSC acquire：屏障必须在「装载 rear」之后、「读数据」之前——与 Linux kfifo
     * __kfifo_out 的 smp_rmb 同位置。控制依赖（memcpy 长度依赖 used）并不约束
     * load-load 顺序：M7 这类核可对 Normal 内存投机预取，若把屏障放在装载 rear
     * 之前，会出现「看到新 rear、却读到屏障前已投机预取的旧数据」。
     * Peek 同样要读数据，因此 Peek 路径也必须有这道屏障。 */
    RB_Barrier(sync);

    if (used < *size) {
        if (isForce) {
            *size = used;
        } else {
#if RB_ENABLE_STATS
            rb->underflow_cnt++;
#endif
            return RB_RET(RET_CLASS_DATA, RET_R_DATA_NOT_ENOUGH);
        }
    }
    if (*size == 0) return RET_OK;

    /* front_index 是自由递增序号，先 & mask 取物理槽下标；end_size 是从该槽到物理
     * 尾部的连续距离。第一段读 min(want, end_size)，剩余从物理 0 处续读。 */
    const uint32_t rpos     = rb->front_index & rb->mask;
    const uint32_t end_size = rb->size - rpos;
    if (end_size >= *size) {
        memcpy(add, rb->buffer + rpos, *size);
    } else {
        memcpy(add, rb->buffer + rpos, end_size);
        memcpy(add + end_size, rb->buffer, *size - end_size);
    }
    /* SPSC release：保证 memcpy 读完才更新 front_index，否则生产者可能
     * 看到新的 front_index 但消费者还没真的读完。 */
    if (!isPeek) {
        RB_Barrier(sync);
    }

    if (!isPeek) {
        rb->front_index = RB_NextIndex(rb, rb->front_index, *size);
    }
    /* 水位线更新策略：
     *   - 模型 A（带锁，RB_SYNC_NONE）：消费者侧也更新，受临界区保护，安全。
     *   - 模型 B（SPSC）：仅写者侧更新；这里跳过，避免与写者 RMW 冲突。
     * Peek 不修改索引也跳过水位线更新（used 没变）。 */
    if (sync == RB_SYNC_NONE && !isPeek) {
        RB_UpdateWatermark(rb);
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

    /* 0 请求 */
    if (want == 0) {
        out->p1 = out->p2 = NULL;
        out->n1 = out->n2 = *granted = 0;
        return RET_OK;
    }

    /* 空间检查 */
    if (want > limit) {
        if (isCompatible) {
            want = limit;
        } else {
            return isWrite ? RB_RET(RET_CLASS_RESOURCE, RET_R_NO_MEM)
                           : RB_RET(RET_CLASS_DATA, RET_R_DATA_NOT_ENOUGH);
        }
    }

    /* 兼容模式下如果限制为 0 */
    if (want == 0) {
        out->p1 = out->p2 = NULL;
        out->n1 = out->n2 = *granted = 0;
        return RET_OK;
    }

    /* 复制索引到本地，避免 SPSC 下读两次 volatile 出现不一致快照。
     * 注：模型 A 在临界区内调用，复制也无害。 */
    const uint32_t size = rb->size;
    const uint32_t mask = rb->mask;

    /* 自由递增方案下，写/读窗口都从「当前序号 & mask」对应的物理槽起，按物理尾
     * 边界拆成最多两段。want 此处已 <= limit（remain 或 used），故两段之和不会
     * 越过对端、也不会超过整段容量；不再需要哨兵特判（去掉了旧版 front==0 时
     * tailFree-- 和 front-rear-1 这类留 1 字节的逻辑）。 */
    const uint32_t start_idx = isWrite ? rb->rear_index : rb->front_index;
    const uint32_t first     = start_idx & mask;   /* 起点在物理数组的下标 */
    const uint32_t to_end    = size - first;       /* 从该槽到物理尾部的连续字节数 */
    const uint32_t n1        = (want < to_end) ? want : to_end;
    const uint32_t n2        = want - n1;

    out->p1  = (n1 > 0) ? (rb->buffer + first) : NULL;
    out->p2  = (n2 > 0) ? (rb->buffer) : NULL;
    out->n1  = n1;
    out->n2  = n2;
    *granted = n1 + n2;
    return RET_OK;
}

/**============================================================================================ */
/**==================================      Span 拷贝工具    ===================================== */
/**============================================================================================ */
/**
 * @brief 将线性缓冲区数据写入 span 描述的两段目标空间
 * @param span 由 Reserve 接口返回的目标窗口（p1/n1 + p2/n2）
 * @param src  线性源数据地址
 * @param len  想要拷贝的字节数
 * @note 实际写入字节数 = min(len, span->n1 + span->n2)
 */
void RingBuffer_SpanWriteFromLinear(const RingBufferSpan* span, const uint8_t* src, uint32_t len) {
    ASSERT_PARAM((span != NULL) && (src != NULL) && (len != 0u));
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
    ASSERT_PARAM((span != NULL) && (dst != NULL) && (len != 0u));
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
 * @brief 从环形源缓冲区拷贝数据到 span 目标窗口（支持源端回绕一次）
 * @param span         目标窗口（通常由 WriteReserve 返回）
 * @param src_ring     环形源缓冲区首地址（如 DMA circular buffer）
 * @param src_ring_len 环形源缓冲区长度（字节）
 * @param src_pos      源端起始索引
 * @param len          想要拷贝的字节数
 * @note 实际写入字节数 = min(len, src_ring_len, span->n1 + span->n2)
 * @note 源端是「一段长度 <= src_ring_len 的逻辑数据、最多绕环尾一次」。len 超过
 *       src_ring_len 没有意义（再多就要重复读同一批源字节），且若不夹住会让
 *       回绕分支的第二段 memcpy 读越界——这里把 len 夹到 src_ring_len 兜底。
 */
void RingBuffer_SpanWriteFromCircular(const RingBufferSpan* span, const uint8_t* src_ring,
                                      uint32_t src_ring_len, uint32_t src_pos, uint32_t len) {
    ASSERT_PARAM((span != NULL) && (src_ring != NULL) && (src_ring_len != 0u) && (len != 0u));
    if (!span || !src_ring || src_ring_len == 0u || len == 0u) return;

    /* 夹住 len：源环最多提供 src_ring_len 个不同字节，超过会让下面回绕分支的
     * 「n - tail」大于源环长而读越界。 */
    if (len > src_ring_len) len = src_ring_len;

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

/**============================================================================================ */
/**==================================         BASE        ===================================== */
/**============================================================================================ */

/* 把 v 向上取整到最近的 2 的幂（v<=1 返回 1）。容量取 2 的幂，序号才能用 &mask
 * 定位、并在 UINT32_MAX 回绕点保持物理槽连续——这是自由递增方案成立的前提。
 * v 超过 0x80000000（最大可表示的 2 的幂）时无法向上取整，返回 0 让调用方判失败。 */
static inline uint32_t RB_RoundUpPow2(uint32_t v) {
    if (v <= 1u) return 1u;
    if (v > 0x80000000u) return 0u; /* 已超出 uint32 能表示的最大 2 的幂 */
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1u;
}

ret_code_t CreateRingBuffer(RingBuffer* rb, const char* name, const uint32_t size) {
    ASSERT_PARAM((rb != NULL) && (size >= 2u));
    if (rb == NULL || size < 2) return RB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);

    /* 强制 2 的幂：把请求的 size 向上取整（如 1000 → 1024）。自由递增 + &mask 定位
     * 只在 2 的幂下正确，且取整后整段容量都可用（无哨兵）。实际分配按取整后的
     * 大小，调用方用 rb->size 读回真实容量。 */
    const uint32_t cap = RB_RoundUpPow2(size);
    if (cap == 0u) {
        ASSERT_PARAM(cap != 0u);
        return RB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    }

    rb->buffer = static_alloc(cap, DEFAULT_ALIGNMENT);
    if (rb->buffer == NULL) {
        memset(rb, 0, sizeof(RingBuffer));
        return RB_RET(RET_CLASS_RESOURCE, RET_R_NO_MEM);
    }

    rb->name              = name;
    rb->front_index       = 0;
    rb->rear_index        = 0;
    rb->size              = cap;
    rb->mask              = cap - 1u;
#if RB_ENABLE_STATS
    rb->high_watermark_used = 0;
    rb->overflow_cnt        = 0;
    rb->underflow_cnt       = 0;
    rb->last_remain         = 0;
    rb->last_used           = 0;
#endif
    return RET_OK;
}

/**
 * @brief 线程版获取使用的容量字节数
 * @param rb RB句柄
 * @return 已经使用的容量字节数
 */
uint32_t RingBuffer_GetUsedSize(const RingBuffer* rb) {
    RB_CHECK_VALID_U32(rb);
    rb_isr_state_t s;
    RB_ENTER_CRITICAL(s);
    const uint32_t ret = RB_GetUsed_Core(rb);
    RB_EXIT_CRITICAL(s);
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
    rb_isr_state_t s;
    RB_ENTER_CRITICAL(s);
    const uint32_t ret = RB_GetRemain_Core(rb);
    RB_EXIT_CRITICAL(s);
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
    rb_isr_state_t s;
    RB_ENTER_CRITICAL(s);
    const ret_code_t ret = RB_Write_Logic(rb, add, size, isForceWrite, RB_SYNC_NONE);
    RB_EXIT_CRITICAL(s);
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
    const ret_code_t ret = RB_Write_Logic(rb, add, size, isForceWrite, RB_SYNC_NONE);
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
    rb_isr_state_t s;
    RB_ENTER_CRITICAL(s);
    const ret_code_t ret = RB_Read_Logic(rb, add, size, isForceRead, false, RB_SYNC_NONE);
    RB_EXIT_CRITICAL(s);
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
    const ret_code_t ret = RB_Read_Logic(rb, add, size, isForceRead, false, RB_SYNC_NONE);
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
    rb_isr_state_t s;
    RB_ENTER_CRITICAL(s);
    const ret_code_t ret = RB_Read_Logic(rb_mut, add, size, isForcePeek, true, RB_SYNC_NONE);
    RB_EXIT_CRITICAL(s);
    return ret;
}

/**
 * @brief 线程版 重置 RB 空间
 * @param rb RB句柄
 * @return 状态码
 * @warning 本函数同时写 front/rear 两个索引，违反 SPSC「每个索引只有一个写者」
 *          的铁律；内部临界区只能挡住本核中断，挡不住无锁对端、另一个核或 DMA。
 *          因此仅当生产者与消费者都已停止（静止态：初始化、错误恢复且对端已关）
 *          时才允许调用。运行期想丢弃积压数据请用 RingBuffer_ResetByConsumer()。
 */
ret_code_t ResetRingBuffer(RingBuffer* rb) {
    RB_CHECK_VALID_RC(rb);
    rb_isr_state_t s;
    RB_ENTER_CRITICAL(s);
    rb->front_index = 0;
    rb->rear_index  = 0;
    RB_EXIT_CRITICAL(s);
    return RET_OK;
}

/**
 * @brief 中断版 重置 RB 空间
 * @param rb RB句柄
 * @return 状态码
 * @warning 同 ResetRingBuffer：仅当生产者与消费者都已停止时才允许调用。
 */
ret_code_t ResetRingBufferFromISR(RingBuffer* rb) {
    RB_CHECK_VALID_RC(rb);
    rb_isr_state_t saved;
    RB_ENTER_CRITICAL_FROM_ISR(saved);
    rb->front_index = 0;
    rb->rear_index  = 0;
    RB_EXIT_CRITICAL_FROM_ISR(saved);
    return RET_OK;
}

/**
 * @brief 消费者侧安全清空：丢弃当前所有已入队数据（front 追平 rear）
 * @param rb RB句柄
 * @return 状态码
 * @note 对标 Linux kfifo_reset_out()。只写消费者自己的 front_index、只读生产者
 *       的 rear_index，保持 SPSC「每个索引只有一个写者」的铁律——因此生产者
 *       （软件流或 DMA）仍在运行时也可安全调用。若读到的 rear 是旧值，则只丢弃
 *       「已看到」的数据，之后新入队的保留——语义即「清掉我所见的积压」。
 * @note 与 ResetRingBuffer 的区别：后者把两个索引清零，是对两个索引的"第二写者"，
 *       仅静止态可用；本函数才是运行期丢弃数据（如错误恢复时清掉 UART 烂包）的
 *       正确入口。仅限消费者上下文调用。
 */
ret_code_t RingBuffer_ResetByConsumer(RingBuffer* rb) {
    RB_CHECK_VALID_RC(rb);
    rb->front_index = rb->rear_index;
    return RET_OK;
}

/**============================================================================================ */
/**==================================         零拷贝         ==================================== */
/**============================================================================================ */

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
    rb_isr_state_t s;
    RB_ENTER_CRITICAL(s);
    const ret_code_t ret = RB_Reserve_Logic(rb, want, out, granted, isCompatible, true);
    RB_EXIT_CRITICAL(s);
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
    RB_UpdateWatermark(rb);

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
    rb_isr_state_t s;
    RB_ENTER_CRITICAL(s);
    const ret_code_t ret = RB_Commit_Write_Logic(rb, commit);
    RB_EXIT_CRITICAL(s);
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
    rb_isr_state_t s;
    RB_ENTER_CRITICAL(s);
    const ret_code_t ret = RB_Reserve_Logic(rb, want, out, granted, isCompatible, false);
    RB_EXIT_CRITICAL(s);
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
        if (isCompatible) {
            g = used;
        } else {
            return RB_RET(RET_CLASS_DATA, RET_R_DATA_NOT_ENOUGH);
        }
    }

    if (actual_drop) *actual_drop = g;
    if (g > 0) {
        rb->front_index = RB_NextIndex(rb, rb->front_index, g);
        /* 成功读取：更新水位线 */
        RB_UpdateWatermark(rb);
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
    rb_isr_state_t s;
    RB_ENTER_CRITICAL(s);
    const ret_code_t ret = RB_Commit_Read_Logic(rb, commit, NULL, false);
    RB_EXIT_CRITICAL(s);
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
    rb_isr_state_t s;
    RB_ENTER_CRITICAL(s);
    const ret_code_t ret = RB_Commit_Read_Logic(rb, drop, dropped, isCompatible);
    RB_EXIT_CRITICAL(s);
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
                                uint8_t isForceWrite, rb_sync_t sync) {
    RB_CHECK_ARGS_RC(rb, add, size);
    return RB_Write_Logic(rb, add, size, isForceWrite, sync);
}
/**
 * @brief SPSC版 从RB 读取数据到 add
 * @param rb RB句柄
 * @param add 数据存储地址
 * @param size 输入： 需要读多少字节数 输出：实际上读的字节数
 * @param isForceRead  true 有多少空间就写多少数据 false 必须全部能够装下才能写入
 * @return 32位分段状态码
 */
ret_code_t ReadRingBuffer_SPSC(RingBuffer* rb, uint8_t* add, uint32_t* size, uint8_t isForceRead,
                               rb_sync_t sync) {
    RB_CHECK_ARGS_RC(rb, add, size);
    return RB_Read_Logic(rb, add, size, isForceRead, false, sync);
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
                               uint8_t isForcePeek, rb_sync_t sync) {
    RB_CHECK_ARGS_RC(rb, add, size);
    return RB_Read_Logic((RingBuffer*)rb, add, size, isForcePeek, true, sync);
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
                                        uint32_t* granted, bool isCompatible, rb_sync_t sync) {
    if (!rb || !out || !granted || !rb->buffer || rb->size < 2)
        return RB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    /* 生产者侧无需前置屏障：front 读到旧值只会让批准的窗口偏小（保守安全），
     * 且调用方随后对窗口的 store 不会被投机提交（参见 RB_Write_Logic 同位置
     * 说明）。数据写完后的发布顺序由 WriteCommit_SPSC 的 release 屏障保证。 */
    (void)sync;
    return RB_Reserve_Logic(rb, want, out, granted, isCompatible, true);
}

/**
 * @brief SPSC 写提交内核：发布 rear_index，release 屏障由 sync 决定。
 * @param rb RB 句柄
 * @param commit 需要提交的字节数（必须 <= Reserve 批准的窗口，调用方保证）
 * @param sync RB_SYNC_SMP：消费者是软件执行流；RB_SYNC_DMA：窗口由 DMA 填充
 */
static inline ret_code_t RB_WriteCommit_SPSC_Core(RingBuffer* rb, uint32_t commit, rb_sync_t sync) {
    ASSERT_PARAM((rb != NULL) && (rb->buffer != NULL) && (rb->size >= 2u));
    /* 去哨兵后满容量是 size，单次 Reserve 在空环时可批准多达 size 字节，故合法
     * commit 上限是 size（不是 size-1）。这里只拦「超过容量」，用 > 不用 >=，
     * 否则会把填满整环的合法 commit==size 误判为非法参数。 */
    if (rb == NULL || rb->buffer == NULL || rb->size < 2u || commit > rb->size) {
        return RB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    }

    /* SPSC 契约：commit 必须 <= 之前 WriteReserve_SPSC 批准（granted）的窗口，由
     * 调用方保证。这里不再重新读消费者侧的 front_index 去算 remain 做精确校验——
     * 那次读没有前置 acquire 屏障，在多核下是对对端变量的无保护读（data race），
     * 且 lwrb / rte_ring 的 commit 都是无条件推进。
     *
     * release：保证 Reserve 后写入窗口的数据先对消费者可见，再发布 rear_index，
     * 否则消费者（或 DMA）可能看到新的 rear 但旧的数据。 */
    RB_Barrier(sync);
    rb->rear_index = RB_NextIndex(rb, rb->rear_index, commit);
    RB_UpdateWatermark(rb);
    return RET_OK;
}

/**
 * @brief SPSC 写提交：发布 rear_index
 * @param rb RB 句柄
 * @param commit 需要提交的字节数（必须 <= Reserve 批准的窗口）
 * @param sync 对端类型：RB_SYNC_SMP 消费者是软件执行流；RB_SYNC_DMA 窗口由 DMA
 *             填充（单核也发 full-system 屏障，保证数据先落地再发布 rear）
 * @return 32位状态码
 */
ret_code_t RingBuffer_WriteCommit_SPSC(RingBuffer* rb, uint32_t commit, rb_sync_t sync) {
    return RB_WriteCommit_SPSC_Core(rb, commit, sync);
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
                                       uint32_t* granted, bool isCompatible, rb_sync_t sync) {
    if (!rb || !out || !granted || !rb->buffer || rb->size < 2)
        return RB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    const ret_code_t rc = RB_Reserve_Logic(rb, want, out, granted, isCompatible, false);
    /* SPSC acquire：屏障在「Reserve 内部装载 rear」之后、调用方读取窗口数据之前
     * （kfifo smp_rmb 同位置）。若放在 Reserve 之前，挡不住数据区被投机预取——
     * 会出现「看到新 rear、却读到旧数据」。对端是 DMA 生产者时传 RB_SYNC_DMA，
     * 单核也发真屏障，确保读到 DMA 填完后的数据。 */
    RB_Barrier(sync);
    return rc;
}

/**
 * @brief SPSC 读提交内核：发布 front_index，release 屏障由 sync 决定。
 * @param rb RB 句柄
 * @param commit 需要提交的字节数（必须 <= Reserve 批准的窗口，调用方保证）
 * @param sync RB_SYNC_SMP：生产者是软件执行流；RB_SYNC_DMA：窗口由 DMA 读走
 */
static inline ret_code_t RB_ReadCommit_SPSC_Core(RingBuffer* rb, uint32_t commit, rb_sync_t sync) {
    ASSERT_PARAM((rb != NULL) && (rb->buffer != NULL) && (rb->size >= 2u));
    /* 去哨兵后满容量是 size，环满时可读多达 size 字节，合法 commit 上限是 size。
     * 同 WriteCommit：用 > 不用 >=，否则会误拒 commit==size 的合法整环读取。 */
    if (rb == NULL || rb->buffer == NULL || rb->size < 2u || commit > rb->size) {
        return RB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    }

    /* SPSC 契约：commit 必须 <= 之前 ReadReserve_SPSC 批准（granted）的窗口，由
     * 调用方保证。这里不再重新读生产者侧的 rear_index 去算 used 做精确校验——
     * 那次读没有前置 acquire 屏障，多核下是对对端变量的无保护读（data race）。
     *
     * release：保证对窗口的读取已经完成，再发布 front_index，否则生产者（或
     * DMA）可能看到新的 front 就去覆盖一个还没读完的槽。 */
    RB_Barrier(sync);
    rb->front_index = RB_NextIndex(rb, rb->front_index, commit);
    /* 消费者侧不更新水位线（参见文件顶部说明） */
    return RET_OK;
}

/**
 * @brief SPSC 读提交：发布 front_index
 * @param rb RB 句柄
 * @param commit 需要提交的字节数（必须 <= Reserve 批准的窗口）
 * @param sync 对端类型：RB_SYNC_SMP 生产者是软件执行流；RB_SYNC_DMA 窗口由 DMA
 *             读走（单核也发 full-system 屏障，保证 DMA 读完再发布 front）
 * @return 32位状态码
 */
ret_code_t RingBuffer_ReadCommit_SPSC(RingBuffer* rb, uint32_t commit, rb_sync_t sync) {
    return RB_ReadCommit_SPSC_Core(rb, commit, sync);
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
    /* 去哨兵后满容量就是 size，分母用 size（不再 -1）。 */
    const uint32_t cap  = (rb->size > 0u) ? rb->size : 1u;
    /* 用 uint64_t 中间量：used*1000 在 used>~4.29M 时会溢出 uint32_t，
     * 大环（>4MB）下会算错。 */
    uint32_t p          = (uint32_t)(((uint64_t)used * 1000u) / cap);
    if (p > 1000u) p = 1000u;
    return (uint16_t)p;
}
/**
 * @brief 当前的空间占用情况 中断版
 * @param rb RB 句柄
 * @return 当前的空间占用千分比
 */
uint16_t RingBuffer_GetUsedPermilleFromISR(const RingBuffer* rb) {
    RB_CHECK_VALID_U32(rb);
    const uint32_t used = RingBuffer_GetUsedSizeFromISR(rb);
    /* 去哨兵后满容量就是 size，分母用 size（不再 -1）。 */
    const uint32_t cap  = (rb->size > 0u) ? rb->size : 1u;
    uint32_t p          = (uint32_t)(((uint64_t)used * 1000u) / cap);
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

    /* 必须在临界区内一次性取 front/rear 的一致快照：仅把它们各复制到本地变量
     * 只能防「同一变量读两次」，防不住「front 和 rear 来自不同瞬间」——并发写入
     * 时算出的连续段可能越过真实边界，调用方拿去喂 DMA 长度就会越界/漏读。 */
    rb_isr_state_t s;
    RB_ENTER_CRITICAL(s);
    const uint32_t front = rb->front_index;
    const uint32_t rear  = rb->rear_index;
    const uint32_t size  = rb->size;
    const uint32_t mask  = rb->mask;
    RB_EXIT_CRITICAL(s);

    /* 自由递增：已读到的字节数 used = rear-front（无符号减法），连续可读段是从
     * front 物理槽到物理尾的距离与 used 取小者。 */
    const uint32_t used = rear - front;
    const uint32_t rpos = front & mask;
    const uint32_t tail = size - rpos;
    return (used < tail) ? used : tail;
}

/**
 * @brief 获取当前连续可写的字节数（不需要回绕的部分）
 * @param rb RB句柄
 * @return 连续可写字节数
 */
uint32_t RingBuffer_GetContigWrite(const RingBuffer* rb) {
    if (!rb || !rb->buffer || rb->size < 2) return 0;

    /* 同 GetContigRead：临界区内取 front/rear 一致快照，避免撕裂导致返回的
     * 连续可写长度越过真实边界。 */
    rb_isr_state_t s;
    RB_ENTER_CRITICAL(s);
    const uint32_t front = rb->front_index;
    const uint32_t rear  = rb->rear_index;
    const uint32_t size  = rb->size;
    const uint32_t mask  = rb->mask;
    RB_EXIT_CRITICAL(s);

    /* 自由递增：剩余可写 remain = size - used（去哨兵，不再 -1），连续可写段是从
     * rear 物理槽到物理尾的距离与 remain 取小者。去掉了旧版「front==0 时 n1--」的
     * 哨兵特判。 */
    const uint32_t remain = size - (rear - front);
    const uint32_t wpos   = rear & mask;
    const uint32_t tail   = size - wpos;
    return (remain < tail) ? remain : tail;
}

/**
 * @brief 获取指定RB的运行状态快照
 * @param rb RB 句柄
 * @return
 */
RingBufferStatus RingBuffer_GetStatus(const RingBuffer* rb) {
    ASSERT_PARAM(rb != NULL);
    if (rb == NULL) {
        const RingBufferStatus empty = {0};
        return empty;
    }

    rb_isr_state_t s;
    RB_ENTER_CRITICAL(s);

    /* 去哨兵：used = rear-front，remain = size-used，满容量 cap = size。 */
    const uint32_t used   = RB_GetUsed_Core(rb);
    const uint32_t remain = (rb->size > 0u) ? (rb->size - used) : 0u;
    const uint32_t cap    = (rb->size > 0u) ? rb->size : 1u;
    uint32_t permille     = (uint32_t)(((uint64_t)used * 1000u) / cap);
    if (permille > 1000u) permille = 1000u;

    /* contig 直接展开 Core 计算，避免再次进入会取锁的接口。自由递增 + &mask：
     * 连续可读 = min(used, size - (front&mask))；
     * 连续可写 = min(remain, size - (rear&mask))。 */
    const uint32_t front = rb->front_index;
    const uint32_t rear  = rb->rear_index;
    const uint32_t size  = rb->size;
    const uint32_t mask  = rb->mask;
    const uint32_t rpos  = front & mask;
    const uint32_t wpos  = rear & mask;
    const uint32_t r_tail = size - rpos;
    const uint32_t w_tail = size - wpos;
    const uint32_t contig_r = (used < r_tail) ? used : r_tail;
    const uint32_t contig_w = (remain < w_tail) ? remain : w_tail;

    const RingBufferStatus status = {
        .used          = used,
        .remain        = remain,
        .size          = rb->size,
        .used_permille = (uint16_t)permille,
        .empty         = (used == 0u),
        .full          = (remain == 0u),
        .contig_read   = contig_r,
        .contig_write  = contig_w,
#if RB_ENABLE_STATS
        .high_watermark_used = rb->high_watermark_used,
        .overflow_cnt        = rb->overflow_cnt,
        .underflow_cnt       = rb->underflow_cnt,
        .last_remain         = rb->last_remain,
        .last_used           = rb->last_used,
#endif
    };

    RB_EXIT_CRITICAL(s);
    return status;
}

#endif
