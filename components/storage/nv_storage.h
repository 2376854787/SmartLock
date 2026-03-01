#ifndef SMARTLOCK_NV_STORAGE_H
#define SMARTLOCK_NV_STORAGE_H

#include <stdint.h>

#include "flash_partition.h"
#include "ret_code.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 单个 area 配置 */
typedef struct {
    const char *name;           /* area 名称，要求在 nv_storage 配置中唯一 */
    const char *partition_name; /* 所属分区名，需能在 flash_partition 中找到 */
    uint32_t offset;            /* area 在所属分区内的偏移 */
    uint32_t size;              /* area 占用大小，包含 header 与 payload */
    uint16_t format_version;    /* 数据格式版本，用于升级迁移和兼容性判断 */
} nv_storage_area_cfg_t;

/* 多个 area 的配置集合 */
typedef struct {
    const nv_storage_area_cfg_t *areas; /* area 定义数组首地址 */
    uint32_t count;                     /* area 数量 */
} nv_storage_cfg_t;

/* 运行期 area 描述，绑定到具体分区 */
typedef struct {
    const char *name;            /* area 名称 */
    flash_partition_t partition; /* 所属分区的运行时描述 */
    uint32_t offset;             /* area 在分区内的偏移 */
    uint32_t size;               /* area 总大小，包含 header 与 payload */
    uint16_t format_version;     /* 当前 area 期望的数据格式版本 */
} nv_storage_area_t;

/* area 当前已存数据的状态信息 */
typedef struct {
    uint32_t capacity;       /* 当前 area 可用于 payload 的最大容量 */
    uint32_t stored_len;     /* 当前已存 payload 长度 */
    uint16_t format_version; /* 当前记录的数据格式版本 */
} nv_storage_meta_t;

/* nv_storage 当前采用“整区擦除 + header + payload + CRC”模型，不是 KV。 */
ret_code_t nv_storage_init(const nv_storage_cfg_t *cfg);
ret_code_t nv_storage_get(const char *name, nv_storage_area_t *out);
ret_code_t nv_storage_get_meta(const char *name, nv_storage_meta_t *out);
ret_code_t nv_storage_read(const char *name, void *buf, uint32_t buf_size, uint32_t *out_len);
ret_code_t nv_storage_write(const char *name, const void *data, uint32_t len);
ret_code_t nv_storage_erase(const char *name);

#ifdef __cplusplus
}
#endif

#endif  // SMARTLOCK_NV_STORAGE_H
