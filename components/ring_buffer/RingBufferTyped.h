//
// Created by yan on 2026/06/07.
//
// 定长元素队列（槽索引环）。和字节流 RingBuffer 不同：这里 head/tail 是「元素
// 序号」而不是字节偏移，底层是一个 count 个槽的连续数组，每槽放一个元素。
//
// 为什么单独做一套、而不是套在字节流 RB 上：定长队列要的是「按元素」语义，
// 而字节流环留 1 字节哨兵会让容量不是 elem_size 整数倍，回绕点切进元素中间——
// 零拷贝窗口、计数、取模全得做别扭的字节↔元素换算。槽索引把元素当成基本单位，
// 这些问题直接消失，和 DPDK rte_ring 的 slot 模型同构：
//   - count 取 2 的幂，下标用 idx & mask 定位，零除法零取模；
//   - 回绕点天然落在槽边界，零拷贝 span 的两段各是整数个元素，绝不切半；
//   - 元素个数 = head - tail，不需要除法。
//
// 并发模型与字节流 RB 镜像，命名也一致：
//   - 无后缀      ：带锁线程态，内部取临界区
//   - _FromISR    ：带锁 ISR 态，取 ISR 版临界区
//   - _SPSC       ：单生产者单消费者无锁，只用内存屏障；线程态/ISR 态通用，
//                   故不单独提供 _SPSC_FromISR
//   - 零拷贝       ：Reserve/Commit，三种上下文各一套
//
// 同一个实例只能固定用其中一种模型，禁止混用（混用时 SPSC 侧无锁，看不到带锁
// 侧临界区内的中间状态，会数据竞争）。
//

#ifndef RINGBUFFER_TYPED_H
#define RINGBUFFER_TYPED_H

#include <stdbool.h>
#include <stdint.h>

#include "ret_code.h"

/* 定长元素队列句柄（槽索引）。
 * slots 是 count 个槽的连续数组；head/tail 是元素序号，访问槽时 & mask。
 * 留一个空槽区分空与满：head==tail 为空，(head-tail)==count 为满。
 * count 为 2 的幂，mask=count-1，所有定位走位运算。 */
typedef struct {
    const char *name;
    uint8_t *slots;         /* 槽数组首地址，count*elem_size 字节 */
    uint32_t elem_size;     /* 单个元素字节数 */
    uint32_t count;         /* 槽数（2 的幂，含 1 个哨兵槽） */
    uint32_t mask;          /* count - 1 */
    volatile uint32_t head; /* 生产者写位置（元素序号） */
    volatile uint32_t tail; /* 消费者读位置（元素序号） */
} TypedRB;

/* 零拷贝窗口：和字节流 RB 复用同一个 span 结构，但这里 p1/n1、p2/n2 描述的是
 * 「元素」——n1/n2 是元素个数，p1/p2 指向元素数组。两段各是整数个元素。 */
typedef struct {
    uint8_t *p1; /* 第一段首元素地址 */
    uint32_t n1; /* 第一段元素个数 */
    uint8_t *p2; /* 第二段首元素地址（回绕后，可能为 NULL） */
    uint32_t n2; /* 第二段元素个数 */
} TypedRBSpan;

/**============================================================================================ */
/**==================================        创建/复位      ===================================== */
/**============================================================================================ */

ret_code_t TypedRB_Create(TypedRB *t, const char *name, uint32_t count, uint32_t elem_size);

ret_code_t TypedRB_Reset(TypedRB *t);

ret_code_t TypedRB_ResetFromISR(TypedRB *t);

/**============================================================================================ */
/**==================================        状态查询        ==================================== */
/**============================================================================================ */

uint32_t TypedRB_Count(const TypedRB *t);

uint32_t TypedRB_RemainCount(const TypedRB *t);

bool TypedRB_IsFull(const TypedRB *t);

bool TypedRB_IsEmpty(const TypedRB *t);

/**============================================================================================ */
/**==================================      通用带锁（线程态）  ================================== */
/**============================================================================================ */

bool TypedRB_Push(TypedRB *t, const void *elem);

bool TypedRB_Pop(TypedRB *t, void *out);

bool TypedRB_Peek(const TypedRB *t, void *out);

bool TypedRB_Drop(TypedRB *t, uint32_t count, uint32_t *dropped);

bool TypedRB_PushOverwriteOldest(TypedRB *t, const void *elem);

bool TypedRB_OverwriteIfExists(TypedRB *t, const void *elem, uint32_t event_id_off,
                               uint32_t key_off);

/**============================================================================================ */
/**==================================      通用带锁（ISR 态）  ================================== */
/**============================================================================================ */

bool TypedRB_PushFromISR(TypedRB *t, const void *elem);

bool TypedRB_PopFromISR(TypedRB *t, void *out);

bool TypedRB_DropFromISR(TypedRB *t, uint32_t count, uint32_t *dropped);

/**============================================================================================ */
/**==================================          SPSC          ==================================== */
/**============================================================================================ */
/* 注：TypedRB 的 SPSC 屏障固定走 smp_mem_barrier（对端=软件线程/ISR）。定长元素
 * 队列的设计目标是「软件↔软件」的消息/事件传递（如 eventbus mailbox），不面向
 * DMA 对端——DMA 搬运的是字节流，用字节流 RingBuffer 的 *_SPSC(..., RB_SYNC_DMA)
 * 更合适。因此这里不提供 DMA 变体；如将来确有 TypedRB+DMA 需求再按字节流环的
 * sync 参数模式扩展。 */

bool TypedRB_Push_SPSC(TypedRB *t, const void *elem);

bool TypedRB_Pop_SPSC(TypedRB *t, void *out);

bool TypedRB_Peek_SPSC(const TypedRB *t, void *out);

/**============================================================================================ */
/**==================================      零拷贝（线程态）    ================================== */
/**============================================================================================ */

ret_code_t TypedRB_WriteReserve(TypedRB *t, uint32_t want_elems, TypedRBSpan *out,
                                uint32_t *granted_elems, bool isCompatible);

ret_code_t TypedRB_WriteCommit(TypedRB *t, uint32_t commit_elems);

ret_code_t TypedRB_ReadReserve(TypedRB *t, uint32_t want_elems, TypedRBSpan *out,
                               uint32_t *granted_elems, bool isCompatible);

ret_code_t TypedRB_ReadCommit(TypedRB *t, uint32_t commit_elems);

/**============================================================================================ */
/**==================================      零拷贝（ISR 态）    ================================== */
/**============================================================================================ */

ret_code_t TypedRB_WriteReserveFromISR(TypedRB *t, uint32_t want_elems, TypedRBSpan *out,
                                       uint32_t *granted_elems, bool isCompatible);

ret_code_t TypedRB_WriteCommitFromISR(TypedRB *t, uint32_t commit_elems);

ret_code_t TypedRB_ReadReserveFromISR(TypedRB *t, uint32_t want_elems, TypedRBSpan *out,
                                      uint32_t *granted_elems, bool isCompatible);

ret_code_t TypedRB_ReadCommitFromISR(TypedRB *t, uint32_t commit_elems);

/**============================================================================================ */
/**==================================      零拷贝（SPSC）      ================================== */
/**============================================================================================ */

ret_code_t TypedRB_WriteReserve_SPSC(TypedRB *t, uint32_t want_elems, TypedRBSpan *out,
                                     uint32_t *granted_elems, bool isCompatible);

ret_code_t TypedRB_WriteCommit_SPSC(TypedRB *t, uint32_t commit_elems);

ret_code_t TypedRB_ReadReserve_SPSC(TypedRB *t, uint32_t want_elems, TypedRBSpan *out,
                                    uint32_t *granted_elems, bool isCompatible);

ret_code_t TypedRB_ReadCommit_SPSC(TypedRB *t, uint32_t commit_elems);

#endif  // RINGBUFFER_TYPED_H
