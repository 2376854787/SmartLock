#ifndef SMARTLOCK_MEMORY_POOL_H
#define SMARTLOCK_MEMORY_POOL_H
#include <stdint.h>

#include "assert_cus.h"
#include "ret_code_t.h"
/* ===================== 配置 ===================== */

/* cacheline 的大小 */
#ifndef MP_CFG_CACHE_LINE_SIZE
#define MP_CACHE_LINE_SIZE 16 /* 无缓存芯片建议 8或16 , 有则 32或64 */
#endif
/* cpu 字长 */
#ifndef MP_ARCH_ALIGN_SIZE
#define MP_ARCH_ALIGN_SIZE 8 /* cpu 字长或者 8*/
#endif
/* 投毒 */
#ifndef MP_CFG_CANARY
#define MP_CFG_CANARY 1
#endif
/* 隔离 */
#ifndef MP_CFG_QUARANTINE
#define MP_CFG_QUARANTINE 1
#endif
/* DEBUG */
#ifndef MP_CFG_DEBUG
#define MP_CFG_DEBUG 1
#endif

#ifndef MP_ASSERT
#define MP_ASSERT(x) ASSERT_FATAL((x))
#endif

/* 位图和栈大小计算 辅助宏 */
#define MP_CALC_BITMAP_SIZE(n_blks) ((((n_blks) + 31) / 32) * 4)
#define MP_CALC_STACK_SIZE(n_blks)  ((n_blks) * 2)
/* 锁 或者 临界区 */
typedef struct {
    void (*lock)(void *ctx,
                 uint32_t *flags);              /* RTOS 互斥锁锁 返回值无效
                                                    裸机临界区（该情况超时时间参数无效需要自己在实现兼容函数） */
    void (*unlock)(void *ctx, uint32_t *flags); /* 互斥量句柄或者 临界区标志位 */
    void *handle;                               /* 互斥量 需要的句柄 */
} mp_lock_t;
/* 缓存一致性 */
typedef struct {
    void (*invalidate)(void *addr, uint32_t length);
    void (*clean)(void *addr, uint32_t length);
} mp_cache_ops_t;

typedef struct __attribute__((aligned(MP_CACHE_LINE_SIZE))) {
    /*  热点数据  */
    mp_lock_t lock;           /* 锁操作最先触发 */
    uint8_t *base;            /* 内存池基地址 */
    uint16_t *free_stack;     /* 空闲栈 */
    uint32_t *alloc_bm;       /* 位图 */
    uint16_t top;             /* 栈顶指针，频繁读写 */
    uint16_t n_blks;          /* 总块数，边界检查 */
    uint16_t blk_head_offset; /* 块头偏移大小 */

    /*  配置数据 */
    uint16_t blk_total_size;   /* 总大小 */
    uint16_t blk_payload_size; /* 负载大小 */

#if MP_CFG_QUARANTINE
    /*  隔离区数据 */
    uint16_t *q_ring;
    uint16_t q_cap;
    uint16_t q_w;
    uint16_t q_r;
    uint16_t q_cnt;
#endif

    /* 管理/Cache 操作 */
    mp_cache_ops_t cache_ops;
    uint16_t bm_words;

#if MP_CFG_CANARY
    uint32_t canary_head;
    uint32_t canary_tail;
#endif

    /* 统计数据区 */
    struct {
        uint32_t alloc_ok;
        uint32_t alloc_fail;
        uint32_t free_ok;
        uint32_t free_fail;
        uint32_t free_double;
        uint16_t inuse;
        uint16_t max_inuse;
    } stats;
} mp_pool1_t;

/* 初始化配置结构体 */
typedef struct {
    void *pool_mem;     /* 内存池大数组基地址 */
    uint32_t pool_size; /* 大数组的总大小 */

    /* 规格配置 */
    uint16_t n_blks;       /* 期望申请的块数量 */
    uint16_t payload_size; /* 每块给用户的有效负载大小 */

    /* 管理组件 */
    mp_lock_t *lock;           /* 锁/临界区 */
    mp_cache_ops_t *cache_ops; /* Cache 操作 */

    /* 辅助缓冲区 */
    uint16_t *free_stack; /* 栈数组 */
    uint32_t *alloc_bm;   /* 位图数组 */
    uint16_t bm_words;    /* 位图数组长度  */

#if MP_CFG_QUARANTINE
    uint16_t *quarantine_buf; /* 隔离区数组 */
    uint16_t quarantine_cap;  /* 隔离区容量 */
#endif
} mp_config_t;
ret_code_t mp_init(mp_pool1_t *p, const mp_config_t *cfg);
void *mp_alloc(mp_pool1_t *p);
ret_code_t mp_free(mp_pool1_t *p, void *payload_ptr);
ret_code_t mp_check_pool(mp_pool1_t *p);
/**
 * @brief 用于 DMA传输更新内存数据后 cpu读取数据调用
 * @param p 内存池句柄
 * @param payload 负载基地址
 * @note invalidate 函数需要自己实现
 */
void mp_dma_sync_for_cpu(const mp_pool1_t *p, void *payload);
/**
 * @brief 用于cpu 写完后 DMA读取前调用
 * @param p 内存池基地址
 * @param payload 负载基地址
 * @note clean函数需要自己实现
 */
void mp_dma_sync_for_device(const mp_pool1_t *p, void *payload);
#endif  // SMARTLOCK_MEMORY_POOL_H
