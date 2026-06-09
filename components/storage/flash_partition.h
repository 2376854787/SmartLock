#ifndef SMARTLOCK_FLASH_PARTITION_H
#define SMARTLOCK_FLASH_PARTITION_H

#include <stdbool.h>
#include <stdint.h>

#include "ret_code_t.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 分区层只负责“逻辑分区 -> 物理地址区间”的映射，不负责数据格式。 */
typedef enum {
    FLASH_PARTITION_FLAG_NONE     = 0u,
    FLASH_PARTITION_FLAG_READONLY = (1u << 0),
} flash_partition_flag_t;
/* 初始配置使用 */
/* offset/size 相对整片 Flash 基址定义。 */
typedef struct {
    const char *name; /* 分区名，要求在分区表内唯一 */
    uint32_t offset;  /* 相对整片 Flash 基址的偏移 */
    uint32_t size;    /* 分区大小，单位字节 */
    uint32_t flags;   /* 分区属性位，见 flash_partition_flag_t */
} flash_partition_entry_t;
/* 初始配置使用 */
typedef struct {
    const flash_partition_entry_t *entries; /* 分区定义数组首地址 */
    uint32_t count;                         /* 分区数量 */
} flash_partition_cfg_t;

/* 内部使用 */
typedef struct {
    const char *name; /* 分区名 */
    uint32_t base;    /* 分区绝对起始地址 */
    uint32_t size;    /* 分区大小，单位字节 */
    uint32_t flags;   /* 分区属性位，见 flash_partition_flag_t */
} flash_partition_t;

/* 初始化后分区表只读，运行时不支持动态增删 */
ret_code_t flash_partition_init(const flash_partition_cfg_t *cfg);
ret_code_t flash_partition_get_count(uint32_t *out);
ret_code_t flash_partition_get_by_index(uint32_t index, flash_partition_t *out);
ret_code_t flash_partition_get(const char *name, flash_partition_t *out);
/* 访问接口均为同步语义，直接落到 hal_flash_sync 包装。 */
ret_code_t flash_partition_read(const flash_partition_t *part, uint32_t offset, void *dst,
                                uint32_t len);
ret_code_t flash_partition_write(const flash_partition_t *part, uint32_t offset, const void *src,
                                 uint32_t len);
ret_code_t flash_partition_erase(const flash_partition_t *part, uint32_t offset, uint32_t len);
ret_code_t flash_partition_blank_check(const flash_partition_t *part, uint32_t offset, uint32_t len,
                                       bool *out);

#ifdef __cplusplus
}
#endif

#endif  // SMARTLOCK_FLASH_PARTITION_H
