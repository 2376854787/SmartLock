#ifndef SMARTLOCK_MEMORY_POOL_H
#define SMARTLOCK_MEMORY_POOL_H
#include <stdint.h>
typedef enum {
    MP_STATE_FREE  = 0,
    MP_STATE_ALLOC = 1,
} mp_state_t;
/* 每一个块的头部 */
typedef struct {
    uint32_t magic;     /* 魔术数字 */
    uint16_t class_id;  /* 大小标识 */
    uint16_t state;     /* 当前内存块状态 */
    uintptr_t next_enc; /* freelist 的next*/
} mp_block_hdr_t;
/* 内存池 */
typedef struct {
    const char *name;          /* 内存池名称*/
    uint8_t *base;             /* 起始地址 */
    uint32_t blk_size;         /* payload 大小 */
    uint32_t stride;           /* 每个块的步长 */
    uint32_t blk_count;        /* 总块数 */
    mp_block_hdr_t *free_head; /* freelist 头 */
    uint32_t free_blocks;      /*统计 容量*/
    uint32_t min_free_ever;
    uint32_t fail_count;
    uint32_t magic; /* 池级魔数 */
} mp_pool_t;

/* 池管理 */
typedef struct {
    uint32_t boot_key;
    mp_pool_t *pools;
    uint32_t pool_num;
} mp_mgr_t;
#endif  // SMARTLOCK_MEMORY_POOL_H
