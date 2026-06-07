//
// Created by yan on 2026/06/07.
//
// 定长元素队列（槽索引环）的实现。和 DPDK rte_ring 同构：head/tail 是单调递增
// 的元素序号，定位槽时 idx & mask，count 取 2 的幂。这样回绕天然落在槽边界，
// 计数和取模全是位运算，零拷贝窗口两段各是整数个元素——不存在「半个元素」。
//
// head/tail 用 uint32 自由增长、不主动回绕，靠无符号减法 head-tail 直接得到
// 元素个数（即使 head 绕过 0 也正确）。满的判定是 (head-tail)==count，因为留了
// 一个哨兵槽（count 含这个槽），实际可装 count-1 个。
//

#include "APP_config.h"

#if (defined(CFG_FEAT_RINGBUFFER_SYSTEM) && (CFG_FEAT_RINGBUFFER_SYSTEM == 1))
#include <string.h>

#include "MemoryAllocation.h"
#include "RingBufferTyped.h"
#include "assert_cus.h"
#include "barrier.h"
#include "rb_port.h"

#define TRB_RET(clas_, err_) RET_MAKE(RET_MOD_RB, RET_SUB_RB_CORE, RET_CODE_MAKE((clas_), (err_)))

/* 槽数组的内存对齐上限。槽是连续紧排的，槽步长 = elem_size，所以只要首地址对齐
 * 到「能整除 elem_size 的那个对齐」，每个槽就都对齐。8 字节覆盖到 double / uint64_t /
 * 指针，是常见标量的最大自然对齐；更大的对齐需求（如 SIMD 向量）不在本队列目标内。 */
#define TRB_SLOT_ALIGNMENT_MAX 8u

/* 由 elem_size 推导一个安全的槽对齐：取「不超过 elem_size、且能整除 elem_size 的
 * 最大 2 的幂」，再夹到 [1, 8]。
 *   - 定长元素类型的自然对齐必然整除其 sizeof，所以这个值是其对齐的安全上近似；
 *   - 写死 4 会让 8 字节对齐类型（uint64_t、含 double 的结构体）的零拷贝槽指针
 *     可能落在 4 字节边界上，在严格对齐架构（部分 Cortex-M、totally on DMA）上是
 *     未对齐访问。 */
static inline uint8_t trb_slot_alignment(uint32_t elem_size) {
    uint32_t a = TRB_SLOT_ALIGNMENT_MAX;
    while (a > 1u && (elem_size % a) != 0u) {
        a >>= 1;
    }
    return (uint8_t)a;
}

/* 把 v 向上取整到最近的 2 的幂（v<=1 返回 1）。槽数取 2 的幂，下标才能用 &mask。
 * v 超过 0x80000000（最大可表示的 2 的幂）时无法向上取整，返回 0 让调用方判失败。 */
static inline uint32_t trb_round_up_pow2(uint32_t v) {
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

/* 第 idx 号元素（序号）对应的槽地址。idx & mask 定位，零取模。 */
static inline uint8_t *trb_slot(const TypedRB *t, uint32_t idx) {
    return t->slots + (uint32_t)(idx & t->mask) * t->elem_size;
}

/* 当前元素个数。head/tail 自由增长，无符号减法天然处理回绕。 */
static inline uint32_t trb_count(const TypedRB *t) {
    return t->head - t->tail;
}

/**============================================================================================ */
/**==================================        创建/复位      ===================================== */
/**============================================================================================ */

/**
 * @brief 创建至少能容纳 count 个定长元素的队列
 * @param t RB句柄
 * @param name 名字，便于调试
 * @param count 至少要装下的元素个数
 * @param elem_size 单个元素字节数
 * @return 状态码
 */
ret_code_t TypedRB_Create(TypedRB *t, const char *name, uint32_t count, uint32_t elem_size) {
    ASSERT_PARAM((t != NULL) && (count != 0u) && (elem_size != 0u));
    if (t == NULL || count == 0u || elem_size == 0u) {
        return TRB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    }

    /* 要装 count 个，得多留 1 个哨兵槽区分空/满，再向上取整到 2 的幂让 &mask
     * 成立。例如 count=10 → 至少 11 槽 → 取 16 槽。
     *
     * 全程防整数溢出，否则会算出偏小的分配量、之后按错误的 mask 越界写：
     *   1) count+1 不能回绕（count==UINT32_MAX 时 +1 变 0）；
     *   2) 向上取整到 2 的幂不能超过 0x80000000（trb_round_up_pow2 返回 0 表示溢出）；
     *   3) slots*elem_size 这次字节数乘法不能溢出 uint32（用 64 位中间量比较）。 */
    if (count == 0xFFFFFFFFu) {
        return TRB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    }
    const uint32_t slots = trb_round_up_pow2(count + 1u);
    if (slots == 0u) {
        return TRB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    }
    const uint64_t bytes = (uint64_t)slots * (uint64_t)elem_size;
    if (bytes > 0xFFFFFFFFu) {
        return TRB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    }

    t->slots = static_alloc((uint32_t)bytes, trb_slot_alignment(elem_size));
    if (t->slots == NULL) {
        memset(t, 0, sizeof(*t));
        return TRB_RET(RET_CLASS_RESOURCE, RET_R_NO_MEM);
    }

    t->name      = name;
    t->elem_size = elem_size;
    t->count     = slots;
    t->mask      = slots - 1u;
    t->head      = 0u;
    t->tail      = 0u;
    return RET_OK;
}

/**
 * @brief 线程版 复位队列（清空所有元素）
 * @param t RB句柄
 * @return 状态码
 */
ret_code_t TypedRB_Reset(TypedRB *t) {
    ASSERT_PARAM(t != NULL);
    if (t == NULL) return TRB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    rb_isr_state_t s;
    RB_ENTER_CRITICAL(s);
    t->head = 0u;
    t->tail = 0u;
    RB_EXIT_CRITICAL(s);
    return RET_OK;
}

/**
 * @brief 中断版 复位队列（清空所有元素）
 * @param t RB句柄
 * @return 状态码
 */
ret_code_t TypedRB_ResetFromISR(TypedRB *t) {
    ASSERT_PARAM(t != NULL);
    if (t == NULL) return TRB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    rb_isr_state_t s;
    RB_ENTER_CRITICAL_FROM_ISR(s);
    t->head = 0u;
    t->tail = 0u;
    RB_EXIT_CRITICAL_FROM_ISR(s);
    return RET_OK;
}

/**============================================================================================ */
/**==================================        状态查询        ==================================== */
/**============================================================================================ */

/**
 * @brief 获取当前队列中的元素个数
 * @param t RB句柄
 * @return 元素个数（= head - tail）
 */
uint32_t TypedRB_Count(const TypedRB *t) {
    ASSERT_PARAM(t != NULL);
    if (t == NULL) return 0u;
    return trb_count(t);
}

/**
 * @brief 获取当前队列还能再装几个元素
 * @param t RB句柄
 * @return 剩余可装元素个数
 */
uint32_t TypedRB_RemainCount(const TypedRB *t) {
    ASSERT_PARAM(t != NULL);
    if (t == NULL) return 0u;
    /* 可用槽是 count-1（留 1 哨兵），减去已占用的。 */
    return (t->count - 1u) - trb_count(t);
}

/**
 * @brief 判断队列是否已满
 * @param t RB句柄
 * @return true 满
 */
bool TypedRB_IsFull(const TypedRB *t) {
    ASSERT_PARAM(t != NULL);
    if (t == NULL) return false;
    return trb_count(t) == (t->count - 1u);
}

/**
 * @brief 判断队列是否为空
 * @param t RB句柄
 * @return true 空
 */
bool TypedRB_IsEmpty(const TypedRB *t) {
    ASSERT_PARAM(t != NULL);
    if (t == NULL) return true;
    return t->head == t->tail;
}

/**============================================================================================ */
/**==================================      入队/出队 内核     ================================== */
/**============================================================================================ */

/* 写一个元素到 head 槽，不动 head（由调用方在合适时机推进）。 */
static inline void trb_put_at_head(TypedRB *t, const void *elem) {
    memcpy(trb_slot(t, t->head), elem, t->elem_size);
}

/* 从 tail 槽读一个元素，不动 tail。 */
static inline void trb_get_at_tail(const TypedRB *t, void *out) {
    memcpy(out, trb_slot(t, t->tail), t->elem_size);
}

/**============================================================================================ */
/**==================================      通用带锁（线程态）  ================================== */
/**============================================================================================ */

/**
 * @brief 线程版 入队一个元素
 * @param t RB句柄
 * @param elem 指向一个完整元素
 * @return true 写入成功 false 队列满
 */
bool TypedRB_Push(TypedRB *t, const void *elem) {
    ASSERT_PARAM((t != NULL) && (elem != NULL));
    if (t == NULL || elem == NULL) return false;

    rb_isr_state_t s;
    RB_ENTER_CRITICAL(s);
    bool ok = false;
    if (trb_count(t) < (t->count - 1u)) {
        trb_put_at_head(t, elem);
        t->head++;
        ok = true;
    }
    RB_EXIT_CRITICAL(s);
    return ok;
}

/**
 * @brief 线程版 出队一个元素
 * @param t RB句柄
 * @param out 接收元素的缓冲区，至少 elem_size 字节
 * @return true 取出成功 false 队列空
 */
bool TypedRB_Pop(TypedRB *t, void *out) {
    ASSERT_PARAM((t != NULL) && (out != NULL));
    if (t == NULL || out == NULL) return false;

    rb_isr_state_t s;
    RB_ENTER_CRITICAL(s);
    bool ok = false;
    if (t->head != t->tail) {
        trb_get_at_tail(t, out);
        t->tail++;
        ok = true;
    }
    RB_EXIT_CRITICAL(s);
    return ok;
}

/**
 * @brief 线程版 窥视队头元素但不取走
 * @param t RB句柄
 * @param out 接收元素的缓冲区，至少 elem_size 字节
 * @return true 窥视成功 false 队列空
 */
bool TypedRB_Peek(const TypedRB *t, void *out) {
    ASSERT_PARAM((t != NULL) && (out != NULL));
    if (t == NULL || out == NULL) return false;

    rb_isr_state_t s;
    RB_ENTER_CRITICAL(s);
    bool ok = false;
    if (t->head != t->tail) {
        trb_get_at_tail(t, out);
        ok = true;
    }
    RB_EXIT_CRITICAL(s);
    return ok;
}

/**
 * @brief 线程版 丢弃队头若干元素
 * @param t RB句柄
 * @param count 想丢弃的元素个数
 * @param dropped 实际丢弃的元素个数（可为 NULL）
 * @return true 丢弃成功 false 元素不足
 */
bool TypedRB_Drop(TypedRB *t, uint32_t count, uint32_t *dropped) {
    ASSERT_PARAM(t != NULL);
    if (t == NULL) return false;

    rb_isr_state_t s;
    RB_ENTER_CRITICAL(s);
    const uint32_t avail = trb_count(t);
    const uint32_t n     = (count <= avail) ? count : avail;
    t->tail += n;
    RB_EXIT_CRITICAL(s);

    if (dropped) *dropped = n;
    return n == count;
}

/**
 * @brief 线程版 入队，满则丢弃最旧一个再写入
 * @param t RB句柄
 * @param elem 指向一个完整元素
 * @return true 成功（可能丢弃了一个旧元素） false 异常
 */
bool TypedRB_PushOverwriteOldest(TypedRB *t, const void *elem) {
    ASSERT_PARAM((t != NULL) && (elem != NULL));
    if (t == NULL || elem == NULL) return false;

    rb_isr_state_t s;
    RB_ENTER_CRITICAL(s);
    /* 满了就先丢最旧一个，腾出位置。带锁下同时动 head/tail 是安全的。 */
    if (trb_count(t) == (t->count - 1u)) {
        t->tail++;
    }
    trb_put_at_head(t, elem);
    t->head++;
    RB_EXIT_CRITICAL(s);
    return true;
}

/**
 * @brief 线程版 同键去重：找到 (event_id, key) 相同的现有元素并就地覆盖
 * @param t RB句柄
 * @param elem 新元素（整块，elem_size 字节）
 * @param event_id_off 元素内 event_id（uint32_t）的偏移
 * @param key_off 元素内 key（uint32_t）的偏移
 * @return true 找到并覆盖 false 未找到
 * @note 命中时不改读写索引，对消费者透明；查找期间生产者勿并发写
 */
bool TypedRB_OverwriteIfExists(TypedRB *t, const void *elem, uint32_t event_id_off,
                               uint32_t key_off) {
    ASSERT_PARAM((t != NULL) && (elem != NULL));
    ASSERT_PARAM(t != NULL && (event_id_off + 4u <= t->elem_size) &&
                 (key_off + 4u <= t->elem_size));
    if (t == NULL || elem == NULL) return false;
    if (event_id_off + 4u > t->elem_size || key_off + 4u > t->elem_size) return false;

    uint32_t want_id, want_key;
    memcpy(&want_id, (const uint8_t *)elem + event_id_off, 4u);
    memcpy(&want_key, (const uint8_t *)elem + key_off, 4u);

    rb_isr_state_t s;
    RB_ENTER_CRITICAL(s);

    bool hit = false;
    /* 槽索引下元素天然不跨界，字段直接在槽里读，无需线性化、无取模。 */
    for (uint32_t idx = t->tail; idx != t->head; idx++) {
        const uint8_t *slot = trb_slot(t, idx);
        uint32_t cur_id, cur_key;
        memcpy(&cur_id, slot + event_id_off, 4u);
        memcpy(&cur_key, slot + key_off, 4u);
        if (cur_id == want_id && cur_key == want_key) {
            memcpy((void *)slot, elem, t->elem_size);
            hit = true;
            break;
        }
    }

    RB_EXIT_CRITICAL(s);
    return hit;
}

/**============================================================================================ */
/**==================================      通用带锁（ISR 态）  ================================== */
/**============================================================================================ */

/**
 * @brief 中断版 入队一个元素
 * @param t RB句柄
 * @param elem 指向一个完整元素
 * @return true 写入成功 false 队列满
 */
bool TypedRB_PushFromISR(TypedRB *t, const void *elem) {
    ASSERT_PARAM((t != NULL) && (elem != NULL));
    if (t == NULL || elem == NULL) return false;

    rb_isr_state_t s;
    RB_ENTER_CRITICAL_FROM_ISR(s);
    bool ok = false;
    if (trb_count(t) < (t->count - 1u)) {
        trb_put_at_head(t, elem);
        t->head++;
        ok = true;
    }
    RB_EXIT_CRITICAL_FROM_ISR(s);
    return ok;
}

/**
 * @brief 中断版 出队一个元素
 * @param t RB句柄
 * @param out 接收元素的缓冲区，至少 elem_size 字节
 * @return true 取出成功 false 队列空
 */
bool TypedRB_PopFromISR(TypedRB *t, void *out) {
    ASSERT_PARAM((t != NULL) && (out != NULL));
    if (t == NULL || out == NULL) return false;

    rb_isr_state_t s;
    RB_ENTER_CRITICAL_FROM_ISR(s);
    bool ok = false;
    if (t->head != t->tail) {
        trb_get_at_tail(t, out);
        t->tail++;
        ok = true;
    }
    RB_EXIT_CRITICAL_FROM_ISR(s);
    return ok;
}

/**
 * @brief 中断版 丢弃队头若干元素
 * @param t RB句柄
 * @param count 想丢弃的元素个数
 * @param dropped 实际丢弃的元素个数（可为 NULL）
 * @return true 丢弃成功 false 元素不足
 */
bool TypedRB_DropFromISR(TypedRB *t, uint32_t count, uint32_t *dropped) {
    ASSERT_PARAM(t != NULL);
    if (t == NULL) return false;

    rb_isr_state_t s;
    RB_ENTER_CRITICAL_FROM_ISR(s);
    const uint32_t avail = trb_count(t);
    const uint32_t n     = (count <= avail) ? count : avail;
    t->tail += n;
    RB_EXIT_CRITICAL_FROM_ISR(s);

    if (dropped) *dropped = n;
    return n == count;
}

/**============================================================================================ */
/**==================================          SPSC          ==================================== */
/**============================================================================================ */

/**
 * @brief SPSC版 入队一个元素（生产者侧调用）
 * @param t RB句柄
 * @param elem 指向一个完整元素
 * @return true 写入成功 false 队列满
 */
bool TypedRB_Push_SPSC(TypedRB *t, const void *elem) {
    ASSERT_PARAM((t != NULL) && (elem != NULL));
    if (t == NULL || elem == NULL) return false;

    /* acquire：读 tail 之前同步消费者更新 */
    smp_mem_barrier();
    if (trb_count(t) >= (t->count - 1u)) {
        return false; /* 满 */
    }
    trb_put_at_head(t, elem);
    /* release：数据写完才发布 head，否则消费者可能看到新 head 但旧数据 */
    smp_mem_barrier();
    t->head++;
    return true;
}

/**
 * @brief SPSC版 出队一个元素（消费者侧调用）
 * @param t RB句柄
 * @param out 接收元素的缓冲区，至少 elem_size 字节
 * @return true 取出成功 false 队列空
 */
bool TypedRB_Pop_SPSC(TypedRB *t, void *out) {
    ASSERT_PARAM((t != NULL) && (out != NULL));
    if (t == NULL || out == NULL) return false;

    /* acquire：读 head 之前同步生产者更新（确保读到完整数据） */
    smp_mem_barrier();
    if (t->head == t->tail) {
        return false; /* 空 */
    }
    trb_get_at_tail(t, out);
    /* release：数据读完才发布 tail，否则生产者可能覆盖还没读完的槽 */
    smp_mem_barrier();
    t->tail++;
    return true;
}

/**
 * @brief SPSC版 窥视队头元素但不取走（消费者侧调用）
 * @param t RB句柄
 * @param out 接收元素的缓冲区，至少 elem_size 字节
 * @return true 窥视成功 false 队列空
 */
bool TypedRB_Peek_SPSC(const TypedRB *t, void *out) {
    ASSERT_PARAM((t != NULL) && (out != NULL));
    if (t == NULL || out == NULL) return false;

    smp_mem_barrier();
    if (t->head == t->tail) {
        return false;
    }
    trb_get_at_tail(t, out);
    return true; /* Peek 不动 tail，无需 release */
}

/**============================================================================================ */
/**==================================      零拷贝 换算内核     ================================== */
/**============================================================================================ */

/* 把「从 start 序号起、want 个元素」的连续区域拆成最多两段槽窗口（按物理数组
 * 回绕点 count 拆）。limit 是这一侧最多可用的元素数（写=空闲槽，读=已用元素）。
 * 槽索引下两段天然各是整数个元素，这就是相比字节流壳的根本好处。 */
static ret_code_t trb_reserve_core(TypedRB *t, uint32_t start, uint32_t limit, uint32_t want,
                                   TypedRBSpan *out, uint32_t *granted, bool isCompatible,
                                   bool isWrite) {
    if (want == 0u) {
        out->p1 = out->p2 = NULL;
        out->n1 = out->n2 = *granted = 0u;
        return RET_OK;
    }
    if (want > limit) {
        if (isCompatible) {
            want = limit;
        } else {
            return isWrite ? TRB_RET(RET_CLASS_RESOURCE, RET_R_NO_MEM)
                           : TRB_RET(RET_CLASS_DATA, RET_R_DATA_NOT_ENOUGH);
        }
    }
    if (want == 0u) {
        out->p1 = out->p2 = NULL;
        out->n1 = out->n2 = *granted = 0u;
        return RET_OK;
    }

    const uint32_t first_slot = start & t->mask;       /* 起点在物理数组的下标 */
    const uint32_t to_end     = t->count - first_slot; /* 到数组尾部的连续槽数 */
    const uint32_t n1         = (want < to_end) ? want : to_end;
    const uint32_t n2         = want - n1;

    out->p1                   = t->slots + (uint32_t)first_slot * t->elem_size;
    out->n1                   = n1;
    out->p2                   = (n2 > 0u) ? t->slots : NULL;
    out->n2                   = n2;
    *granted                  = want;
    return RET_OK;
}

/**============================================================================================ */
/**==================================      零拷贝（线程态）    ================================== */
/**============================================================================================ */

/**
 * @brief 线程版 预约可写窗口（按元素）
 * @param t RB句柄
 * @param want_elems 想写入的元素个数
 * @param out 返回可写窗口；n1/n2 是元素个数，两段各是整数个元素
 * @param granted_elems 实际批准的元素个数
 * @param isCompatible true 有多少批准多少 false 必须全部满足才批准
 * @return 状态码
 */
ret_code_t TypedRB_WriteReserve(TypedRB *t, uint32_t want_elems, TypedRBSpan *out,
                                uint32_t *granted_elems, bool isCompatible) {
    ASSERT_PARAM((t != NULL) && (out != NULL) && (granted_elems != NULL));
    if (!t || !out || !granted_elems) return TRB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    rb_isr_state_t s;
    RB_ENTER_CRITICAL(s);
    const uint32_t free = (t->count - 1u) - trb_count(t);
    const ret_code_t rc =
        trb_reserve_core(t, t->head, free, want_elems, out, granted_elems, isCompatible, true);
    RB_EXIT_CRITICAL(s);
    return rc;
}

/**
 * @brief 线程版 提交实际写入的元素个数
 * @param t RB句柄
 * @param commit_elems 实际写入的元素个数
 * @return 状态码
 */
ret_code_t TypedRB_WriteCommit(TypedRB *t, uint32_t commit_elems) {
    ASSERT_PARAM(t != NULL);
    if (!t) return TRB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    rb_isr_state_t s;
    RB_ENTER_CRITICAL(s);
    if (commit_elems > (t->count - 1u) - trb_count(t)) {
        RB_EXIT_CRITICAL(s);
        return TRB_RET(RET_CLASS_RESOURCE, RET_R_NO_MEM);
    }
    t->head += commit_elems;
    RB_EXIT_CRITICAL(s);
    return RET_OK;
}

/**
 * @brief 线程版 预约可读窗口（按元素）
 * @param t RB句柄
 * @param want_elems 想读取的元素个数
 * @param out 返回可读窗口；n1/n2 是元素个数，两段各是整数个元素
 * @param granted_elems 实际批准的元素个数
 * @param isCompatible true 有多少批准多少 false 必须全部满足才批准
 * @return 状态码
 */
ret_code_t TypedRB_ReadReserve(TypedRB *t, uint32_t want_elems, TypedRBSpan *out,
                               uint32_t *granted_elems, bool isCompatible) {
    ASSERT_PARAM((t != NULL) && (out != NULL) && (granted_elems != NULL));
    if (!t || !out || !granted_elems) return TRB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    rb_isr_state_t s;
    RB_ENTER_CRITICAL(s);
    const ret_code_t rc = trb_reserve_core(t, t->tail, trb_count(t), want_elems, out, granted_elems,
                                           isCompatible, false);
    RB_EXIT_CRITICAL(s);
    return rc;
}

/**
 * @brief 线程版 提交实际读取的元素个数
 * @param t RB句柄
 * @param commit_elems 实际读取的元素个数
 * @return 状态码
 */
ret_code_t TypedRB_ReadCommit(TypedRB *t, uint32_t commit_elems) {
    ASSERT_PARAM(t != NULL);
    if (!t) return TRB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    rb_isr_state_t s;
    RB_ENTER_CRITICAL(s);
    if (commit_elems > trb_count(t)) {
        RB_EXIT_CRITICAL(s);
        return TRB_RET(RET_CLASS_DATA, RET_R_DATA_NOT_ENOUGH);
    }
    t->tail += commit_elems;
    RB_EXIT_CRITICAL(s);
    return RET_OK;
}

/**============================================================================================ */
/**==================================      零拷贝（ISR 态）    ================================== */
/**============================================================================================ */

/**
 * @brief 中断版 预约可写窗口（按元素）
 * @param t RB句柄
 * @param want_elems 想写入的元素个数
 * @param out 返回可写窗口
 * @param granted_elems 实际批准的元素个数
 * @param isCompatible true 有多少批准多少 false 必须全部满足才批准
 * @return 状态码
 */
ret_code_t TypedRB_WriteReserveFromISR(TypedRB *t, uint32_t want_elems, TypedRBSpan *out,
                                       uint32_t *granted_elems, bool isCompatible) {
    ASSERT_PARAM((t != NULL) && (out != NULL) && (granted_elems != NULL));
    if (!t || !out || !granted_elems) return TRB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    rb_isr_state_t s;
    RB_ENTER_CRITICAL_FROM_ISR(s);
    const uint32_t free = (t->count - 1u) - trb_count(t);
    const ret_code_t rc =
        trb_reserve_core(t, t->head, free, want_elems, out, granted_elems, isCompatible, true);
    RB_EXIT_CRITICAL_FROM_ISR(s);
    return rc;
}

/**
 * @brief 中断版 提交实际写入的元素个数
 * @param t RB句柄
 * @param commit_elems 实际写入的元素个数
 * @return 状态码
 */
ret_code_t TypedRB_WriteCommitFromISR(TypedRB *t, uint32_t commit_elems) {
    ASSERT_PARAM(t != NULL);
    if (!t) return TRB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    rb_isr_state_t s;
    RB_ENTER_CRITICAL_FROM_ISR(s);
    if (commit_elems > (t->count - 1u) - trb_count(t)) {
        RB_EXIT_CRITICAL_FROM_ISR(s);
        return TRB_RET(RET_CLASS_RESOURCE, RET_R_NO_MEM);
    }
    t->head += commit_elems;
    RB_EXIT_CRITICAL_FROM_ISR(s);
    return RET_OK;
}

/**
 * @brief 中断版 预约可读窗口（按元素）
 * @param t RB句柄
 * @param want_elems 想读取的元素个数
 * @param out 返回可读窗口
 * @param granted_elems 实际批准的元素个数
 * @param isCompatible true 有多少批准多少 false 必须全部满足才批准
 * @return 状态码
 */
ret_code_t TypedRB_ReadReserveFromISR(TypedRB *t, uint32_t want_elems, TypedRBSpan *out,
                                      uint32_t *granted_elems, bool isCompatible) {
    ASSERT_PARAM((t != NULL) && (out != NULL) && (granted_elems != NULL));
    if (!t || !out || !granted_elems) return TRB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    rb_isr_state_t s;
    RB_ENTER_CRITICAL_FROM_ISR(s);
    const ret_code_t rc = trb_reserve_core(t, t->tail, trb_count(t), want_elems, out, granted_elems,
                                           isCompatible, false);
    RB_EXIT_CRITICAL_FROM_ISR(s);
    return rc;
}

/**
 * @brief 中断版 提交实际读取的元素个数
 * @param t RB句柄
 * @param commit_elems 实际读取的元素个数
 * @return 状态码
 */
ret_code_t TypedRB_ReadCommitFromISR(TypedRB *t, uint32_t commit_elems) {
    ASSERT_PARAM(t != NULL);
    if (!t) return TRB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    rb_isr_state_t s;
    RB_ENTER_CRITICAL_FROM_ISR(s);
    if (commit_elems > trb_count(t)) {
        RB_EXIT_CRITICAL_FROM_ISR(s);
        return TRB_RET(RET_CLASS_DATA, RET_R_DATA_NOT_ENOUGH);
    }
    t->tail += commit_elems;
    RB_EXIT_CRITICAL_FROM_ISR(s);
    return RET_OK;
}

/**============================================================================================ */
/**==================================      零拷贝（SPSC）      ================================== */
/**============================================================================================ */

/**
 * @brief SPSC版 预约可写窗口（按元素，生产者侧调用）
 * @param t RB句柄
 * @param want_elems 想写入的元素个数
 * @param out 返回可写窗口
 * @param granted_elems 实际批准的元素个数
 * @param isCompatible true 有多少批准多少 false 必须全部满足才批准
 * @return 状态码
 */
ret_code_t TypedRB_WriteReserve_SPSC(TypedRB *t, uint32_t want_elems, TypedRBSpan *out,
                                     uint32_t *granted_elems, bool isCompatible) {
    ASSERT_PARAM((t != NULL) && (out != NULL) && (granted_elems != NULL));
    if (!t || !out || !granted_elems) return TRB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    smp_mem_barrier(); /* acquire：读 tail 之前同步消费者 */
    const uint32_t free = (t->count - 1u) - trb_count(t);
    return trb_reserve_core(t, t->head, free, want_elems, out, granted_elems, isCompatible, true);
}

/**
 * @brief SPSC版 提交实际写入的元素个数（生产者侧调用）
 * @param t RB句柄
 * @param commit_elems 实际写入的元素个数
 * @return 状态码
 */
ret_code_t TypedRB_WriteCommit_SPSC(TypedRB *t, uint32_t commit_elems) {
    /* 句柄 + 上界的便宜无竞争校验：commit_elems 不可能 >= count（容量上限 count-1），
     * count 在 Create 后只读、对端不写，所以不重新引入对 tail 的无保护读。 */
    ASSERT_PARAM((t != NULL) && (t->slots != NULL));
    if (!t || t->slots == NULL || commit_elems >= t->count) {
        return TRB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    }
    /* SPSC 契约：commit_elems <= 之前 WriteReserve_SPSC 批准的窗口，由调用方
     * 保证。不再用 trb_count() 重读消费者 tail 做精确校验——那次读无前置 acquire
     * 屏障，多核下是 data race；commit 应无条件推进（同 rte_ring）。 */
    smp_mem_barrier(); /* release：窗口数据写完才发布 head */
    t->head += commit_elems;
    return RET_OK;
}

/**
 * @brief SPSC版 预约可读窗口（按元素，消费者侧调用）
 * @param t RB句柄
 * @param want_elems 想读取的元素个数
 * @param out 返回可读窗口
 * @param granted_elems 实际批准的元素个数
 * @param isCompatible true 有多少批准多少 false 必须全部满足才批准
 * @return 状态码
 */
ret_code_t TypedRB_ReadReserve_SPSC(TypedRB *t, uint32_t want_elems, TypedRBSpan *out,
                                    uint32_t *granted_elems, bool isCompatible) {
    ASSERT_PARAM((t != NULL) && (out != NULL) && (granted_elems != NULL));
    if (!t || !out || !granted_elems) return TRB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    smp_mem_barrier(); /* acquire：读 head 之前同步生产者 */
    return trb_reserve_core(t, t->tail, trb_count(t), want_elems, out, granted_elems, isCompatible,
                            false);
}

/**
 * @brief SPSC版 提交实际读取的元素个数（消费者侧调用）
 * @param t RB句柄
 * @param commit_elems 实际读取的元素个数
 * @return 状态码
 */
ret_code_t TypedRB_ReadCommit_SPSC(TypedRB *t, uint32_t commit_elems) {
    /* 句柄 + 上界的便宜无竞争校验，理由同 WriteCommit_SPSC。 */
    ASSERT_PARAM((t != NULL) && (t->slots != NULL));
    if (!t || t->slots == NULL || commit_elems >= t->count) {
        return TRB_RET(RET_CLASS_PARAM, RET_R_INVALID_ARG);
    }
    /* SPSC 契约：commit_elems <= 之前 ReadReserve_SPSC 批准的窗口，由调用方
     * 保证。不再用 trb_count() 重读生产者 head 做精确校验——那次读无前置 acquire
     * 屏障，多核下是 data race。 */
    smp_mem_barrier(); /* release：窗口数据读完才发布 tail */
    t->tail += commit_elems;
    return RET_OK;
}
#endif
