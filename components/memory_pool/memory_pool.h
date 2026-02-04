#ifndef SMARTLOCK_MEMORY_POOL_H
#define SMARTLOCK_MEMORY_POOL_H
#include <stdint.h>

#include "assert_cus.h"
#include "ret_code.h"
/* ===================== 配置 ===================== */
#ifndef MP_CFG_CANARY
#define MP_CFG_CANARY 1
#endif

#ifndef MP_CFG_QUARANTINE
#define MP_CFG_QUARANTINE 1
#endif

#ifndef MP_CFG_DEBUG
#define MP_CFG_DEBUG 1
#endif

#ifndef MP_ASSERT
#define MP_ASSERT(x) ASSERT_FATAL((x))
#endif

typedef struct {
    uint8_t *base;             /* 内存池基地址 */
    uint16_t blk_payload_size; /* 能用的空间大小 */
    uint16_t blk_total_size;   /* payload + canary 头尾 + 对齐填充 */
    uint16_t n_blks;           /*  池子有多少块 */
    uint16_t *free_stack;      /* 存储存储所有空闲块的编号 */
    uint16_t top;              /* 指向free_stack 有效数据的下一个位置 */
    uint32_t *assoc_bm;        /* 位图 区分块是否是空闲 */
    uint16_t bm_words;         /* 位图的页数 */
#if MP_CFG_QUARANTINE
    /* 隔离 */
    uint16_t *q_ring; /* 环形队列 隔离刚释放的块 */
    uint16_t q_cap;   /* 隔离区容量 */
    uint16_t q_w;     /* 写指针 */
    uint16_t q_r;     /* 读指针 */
    uint16_t q_cnt;   /* 现有多少个块 */
#endif
    /* 统计 */
    uint16_t inuse;       /* 当前分配出去的块数量 */
    uint16_t max_inuse;   /* 历史最高分配量 */
    uint32_t alloc_ok;    /*  成功分配数 */
    uint32_t alloc_fail;  /* 分配失败数 */
    uint32_t free_ok;     /* 释放成功数 */
    uint32_t free_fail;   /* 释放失败数 */
    uint32_t free_double; /* 重复释放数 */
#if MP_CFG_CANARY
    uint32_t canary_head; /* 头魔数 */
    uint32_t canary_tail; /* 尾魔数 */
#endif
} mp_pool_t;
ret_code_t mp_init(mp_pool_t *p, void *pool_mem, uint16_t n_blks, uint16_t payload_size,
                   uint16_t *free_stack_buf, uint32_t *alloc_bitmap_buf, uint16_t bitmap_words,
                   uint16_t *quarantine_buf, uint16_t quarantine_cap);
void *mp_alloc(mp_pool_t *p);
ret_code_t mp_free(mp_pool_t *p, void *payload_ptr);
ret_code_t mp_check_pool(mp_pool_t *p);

#endif  // SMARTLOCK_MEMORY_POOL_H
