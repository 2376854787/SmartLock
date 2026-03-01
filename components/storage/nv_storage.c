#include "nv_storage.h"

#include "APP_config.h"

#define NV_STORAGE_PARAM(reason_) RET_MAKE_PARAM(RET_MOD_STOR, RET_SUB_STOR_NVS, (reason_))
#define NV_STORAGE_STATE(reason_) RET_MAKE_STATE(RET_MOD_STOR, RET_SUB_STOR_NVS, (reason_))
#define NV_STORAGE_RES(reason_)   RET_MAKE_RESOURCE(RET_MOD_STOR, RET_SUB_STOR_NVS, (reason_))
#define NV_STORAGE_IO(reason_)    RET_MAKE_IO(RET_MOD_STOR, RET_SUB_STOR_NVS, (reason_))
#define NV_STORAGE_DATA(reason_)  RET_MAKE_DATA(RET_MOD_STOR, RET_SUB_STOR_NVS, (reason_))

#if defined(CFG_FEAT_NV_STORAGE) && (CFG_FEAT_NV_STORAGE == 1)

#include <limits.h>
#include <string.h>

#include "assert_cus.h"
#include "crc16.h"

#ifndef CFG_PARAM_NV_STORAGE_MAX_AREAS
#define CFG_PARAM_NV_STORAGE_MAX_AREAS 8u
#endif

#define NV_STORAGE_MAGIC 0x4E565331u

/* 头部常驻在 area 起始处，用于判断有效性与载荷完整性。 */
typedef struct {
    uint32_t magic;          /* 魔数 */
    uint16_t format_version; /* 版本号 */
    uint16_t header_size;    /* 头 大小 */
    uint32_t data_len;       /* 数据大小 */
    uint16_t data_crc;       /* crc */
    uint16_t reserved;       /* 预留 */
} nv_storage_header_t;

typedef struct {
    bool initialized;                                        /* 初始化标志位 */
    uint32_t count;                                          /* nv 数量 */
    nv_storage_area_t areas[CFG_PARAM_NV_STORAGE_MAX_AREAS]; /* 存储所有 nv 的配置信息 */
} nv_storage_ctrl_t;

static nv_storage_ctrl_t s_nvs;

static ret_code_t nv_storage_map_partition_rc(ret_code_t rc) {
    if (ret_is_ok(rc)) return RET_OK;

    if (ret_is_class(rc, RET_CLASS_PARAM)) {
        if (ret_is_reason(rc, RET_R_NULL_PTR)) return NV_STORAGE_PARAM(RET_R_NULL_PTR);
        if (ret_is_reason(rc, RET_R_RANGE_ERR)) return NV_STORAGE_PARAM(RET_R_RANGE_ERR);
        return NV_STORAGE_PARAM(RET_R_INVALID_ARG);
    }
    if (ret_is_class(rc, RET_CLASS_STATE)) {
        if (ret_is_reason(rc, RET_R_BUSY)) return NV_STORAGE_STATE(RET_R_BUSY);
        if (ret_is_reason(rc, RET_R_NOT_READY)) return NV_STORAGE_STATE(RET_R_NOT_READY);
        return NV_STORAGE_STATE(RET_R_STATE_ERR);
    }
    if (ret_is_class(rc, RET_CLASS_RESOURCE)) {
        if (ret_is_reason(rc, RET_R_BUFFER_FULL)) return NV_STORAGE_RES(RET_R_BUFFER_FULL);
        return NV_STORAGE_RES(RET_R_NO_RESOURCE);
    }
    if (ret_is_class(rc, RET_CLASS_TIMEOUT))
        return RET_MAKE_TIMEOUT(RET_MOD_STOR, RET_SUB_STOR_NVS, RET_R_TIMEOUT);
    if (ret_is_class(rc, RET_CLASS_DATA)) return NV_STORAGE_DATA(RET_R_DATA_MISMATCH);
    return NV_STORAGE_IO(RET_R_FLASH_ERR);
}
/**
 * @brief 找出指定 name 的 nv区域
 * @param name 名称
 * @param out 输出指定名称的 nv 区域地址
 * @return
 */
static ret_code_t nv_storage_find(const char *name, nv_storage_area_t **out) {
    /* 参数检查 */
    REQUIRE_RET(s_nvs.initialized, NV_STORAGE_STATE(RET_R_NOT_READY));
    REQUIRE_RET(name != NULL, NV_STORAGE_PARAM(RET_R_NULL_PTR));
    REQUIRE_RET(out != NULL, NV_STORAGE_PARAM(RET_R_NULL_PTR));
    /* 遍历检查名称符合的 nv 区域描述结构体信息返回 */
    for (uint32_t i = 0; i < s_nvs.count; ++i) {
        if (strcmp(s_nvs.areas[i].name, name) == 0) {
            *out = &s_nvs.areas[i];
            return RET_OK;
        }
    }
    return NV_STORAGE_PARAM(RET_R_RANGE_ERR);
}

static uint32_t nv_storage_payload_capacity(const nv_storage_area_t *area) {
    return area->size - (uint32_t)sizeof(nv_storage_header_t);
}

static ret_code_t nv_storage_read_header(const nv_storage_area_t *area, nv_storage_header_t *hdr) {
    const ret_code_t rc =
        flash_partition_read(&area->partition, area->offset, hdr, (uint32_t)sizeof(*hdr));
    return nv_storage_map_partition_rc(rc);
}

static ret_code_t nv_storage_validate_header(const nv_storage_area_t *area,
                                             const nv_storage_header_t *hdr) {
    REQUIRE_RET(hdr->magic == NV_STORAGE_MAGIC, NV_STORAGE_STATE(RET_R_NOT_READY));
    REQUIRE_RET(hdr->header_size == (uint16_t)sizeof(*hdr), NV_STORAGE_DATA(RET_R_PARSE_ERR));
    REQUIRE_RET(hdr->format_version == area->format_version, NV_STORAGE_DATA(RET_R_PARSE_ERR));
    REQUIRE_RET(hdr->data_len <= nv_storage_payload_capacity(area),
                NV_STORAGE_DATA(RET_R_DATA_OVERFLOW));
    return RET_OK;
}
/**
 * @brief 将配置转换为 运行态配置
 * @param cfg 配置
 * @return 状态码
 */
ret_code_t nv_storage_init(const nv_storage_cfg_t *cfg) {
    /* 参数检查 */
    REQUIRE_RET(cfg != NULL, NV_STORAGE_PARAM(RET_R_NULL_PTR));
    REQUIRE_RET(cfg->areas != NULL, NV_STORAGE_PARAM(RET_R_NULL_PTR));
    REQUIRE_RET((cfg->count != 0u) && (cfg->count <= CFG_PARAM_NV_STORAGE_MAX_AREAS),
                NV_STORAGE_PARAM(RET_R_RANGE_ERR));
    REQUIRE_RET(!s_nvs.initialized, NV_STORAGE_STATE(RET_R_BUSY));

    memset(&s_nvs, 0, sizeof(s_nvs));

    /* 从配置中获取指定的nv 配置到全局资源池 */
    for (uint32_t i = 0; i < cfg->count; ++i) {
        /* 配置 */
        const nv_storage_area_cfg_t *area_cfg = &cfg->areas[i];
        /* 存储池 */
        nv_storage_area_t *area               = &s_nvs.areas[i];
        uint32_t payload_capacity             = 0u;

        /* 参数检查 */
        REQUIRE_RET((area_cfg->name != NULL) && (area_cfg->name[0] != '\0'),
                    NV_STORAGE_PARAM(RET_R_INVALID_ARG));
        REQUIRE_RET((area_cfg->partition_name != NULL) && (area_cfg->partition_name[0] != '\0'),
                    NV_STORAGE_PARAM(RET_R_INVALID_ARG));
        REQUIRE_RET(area_cfg->size > (uint32_t)sizeof(nv_storage_header_t),
                    NV_STORAGE_PARAM(RET_R_RANGE_ERR));
        payload_capacity = area_cfg->size - (uint32_t)sizeof(nv_storage_header_t);
        REQUIRE_RET(payload_capacity <= UINT16_MAX, NV_STORAGE_PARAM(RET_R_RANGE_ERR));

        /* 根据配置的分区名称获取到对应的分区资源地址 */
        const ret_code_t rc = flash_partition_get(area_cfg->partition_name, &area->partition);
        if (ret_is_err(rc)) return nv_storage_map_partition_rc(rc);
        REQUIRE_RET(area_cfg->offset < area->partition.size, NV_STORAGE_PARAM(RET_R_RANGE_ERR));
        REQUIRE_RET(area_cfg->size <= (area->partition.size - area_cfg->offset),
                    NV_STORAGE_PARAM(RET_R_RANGE_ERR));
        /* 将配置转换为 内部运行态配置 */
        area->name           = area_cfg->name;
        area->offset         = area_cfg->offset;
        area->size           = area_cfg->size;
        area->format_version = area_cfg->format_version;
    }

    /* 同一 partition 内 area 不能重名也不能区间重叠。 */
    for (uint32_t i = 0; i < cfg->count; ++i) {
        const nv_storage_area_t *a = &s_nvs.areas[i];
        for (uint32_t j = i + 1u; j < cfg->count; ++j) {
            const nv_storage_area_t *b = &s_nvs.areas[j];
            const bool same_partition  = (strcmp(a->partition.name, b->partition.name) == 0);
            const uint32_t a_begin     = a->offset;
            const uint32_t a_end       = a_begin + a->size;
            const uint32_t b_begin     = b->offset;
            const uint32_t b_end       = b_begin + b->size;

            REQUIRE_RET(strcmp(a->name, b->name) != 0, NV_STORAGE_PARAM(RET_R_INVALID_ARG));
            if (same_partition) {
                REQUIRE_RET((a_end <= b_begin) || (b_end <= a_begin),
                            NV_STORAGE_PARAM(RET_R_STATE_ERR));
            }
        }
    }

    s_nvs.count       = cfg->count;
    s_nvs.initialized = true;
    return RET_OK;
}
/**
 * @brief 获取nv 在分区里面的 描述信息结构体成员
 * @param name 名字
 * @param out nv 句柄
 * @return 状态码
 */
ret_code_t nv_storage_get(const char *name, nv_storage_area_t *out) {
    nv_storage_area_t *area = NULL;

    REQUIRE_RET(out != NULL, NV_STORAGE_PARAM(RET_R_NULL_PTR));
    const ret_code_t rc = nv_storage_find(name, &area);
    if (ret_is_err(rc)) return rc;
    *out = *area;
    return RET_OK;
}
/**
 * @brief根据nv名称查询其元信息返回
 * @param name 名称
 * @param out 返回元信息
 * @return 状态码
 */
ret_code_t nv_storage_get_meta(const char *name, nv_storage_meta_t *out) {
    nv_storage_area_t *area = NULL;
    nv_storage_header_t hdr = {0};
    ret_code_t rc           = RET_OK;

    REQUIRE_RET(out != NULL, NV_STORAGE_PARAM(RET_R_NULL_PTR));
    /* 查询nv */
    rc = nv_storage_find(name, &area);
    if (ret_is_err(rc)) return rc;

    /* 读取整个头部 */
    rc = nv_storage_read_header(area, &hdr);
    if (ret_is_err(rc)) return rc;

    /* 检查是否是有效的 头部 */
    rc = nv_storage_validate_header(area, &hdr);
    if (ret_is_err(rc)) return rc;

    out->capacity       = nv_storage_payload_capacity(area);
    out->stored_len     = hdr.data_len;
    out->format_version = area->format_version;
    return RET_OK;
}
/**
 *
 * @param name 名称
 * @param buf 接收缓冲区
 * @param buf_size 缓冲区的容量大小
 * @param out_len 实际读取的长度
 * @return 状态码
 */
ret_code_t nv_storage_read(const char *name, void *buf, uint32_t buf_size, uint32_t *out_len) {
    nv_storage_area_t *area = NULL;
    nv_storage_header_t hdr = {0};
    uint16_t crc            = 0u;
    ret_code_t rc           = RET_OK;

    REQUIRE_RET(out_len != NULL, NV_STORAGE_PARAM(RET_R_NULL_PTR));
    /* 获取到nv */
    rc = nv_storage_find(name, &area);
    if (ret_is_err(rc)) return rc;

    /* 获取到头部信息 */
    rc = nv_storage_read_header(area, &hdr);
    if (ret_is_err(rc)) return rc;

    /* 检查头部是否有效 */
    rc = nv_storage_validate_header(area, &hdr);
    if (ret_is_err(rc)) return rc;
    REQUIRE_RET((buf != NULL) || (hdr.data_len == 0u), NV_STORAGE_PARAM(RET_R_NULL_PTR));
    REQUIRE_RET(hdr.data_len <= buf_size, NV_STORAGE_RES(RET_R_BUFFER_FULL));

    /* 读取有效的数据 负载*/
    if (hdr.data_len != 0u) {
        rc = flash_partition_read(&area->partition, area->offset + (uint32_t)sizeof(hdr), buf,
                                  hdr.data_len);
        if (ret_is_err(rc)) return nv_storage_map_partition_rc(rc);
    }

    /* header 与 payload 分离校验，避免把无效长度直接交给上层。 */
    rc = crc16_cal_default_table(CCITT, (const uint8_t *)buf, (uint16_t)hdr.data_len, &crc);
    if (ret_is_err(rc)) return NV_STORAGE_DATA(RET_R_CRC);
    REQUIRE_RET(crc == hdr.data_crc, NV_STORAGE_DATA(RET_R_CRC));

    *out_len = hdr.data_len;
    return RET_OK;
}
/**
 * @brief 写入数据到 nv
 * @param name 名称
 * @param data 数据源
 * @param len 长度
 * @return 状态码
 */
ret_code_t nv_storage_write(const char *name, const void *data, uint32_t len) {
    nv_storage_area_t *area = NULL;
    nv_storage_header_t hdr = {0};
    uint16_t crc            = 0u;
    ret_code_t rc           = RET_OK;

    REQUIRE_RET((data != NULL) || (len == 0u), NV_STORAGE_PARAM(RET_R_NULL_PTR));

    rc = nv_storage_find(name, &area);
    if (ret_is_err(rc)) return rc;
    REQUIRE_RET(len <= nv_storage_payload_capacity(area), NV_STORAGE_PARAM(RET_R_RANGE_ERR));
    REQUIRE_RET(len <= UINT16_MAX, NV_STORAGE_PARAM(RET_R_RANGE_ERR));

    /* 计算数据CRC */
    rc = crc16_cal_default_table(CCITT, (const uint8_t *)data, (uint16_t)len, &crc);
    if (ret_is_err(rc)) return NV_STORAGE_DATA(RET_R_CRC);

    /* 填写头部信息 */
    hdr.magic          = NV_STORAGE_MAGIC;
    hdr.format_version = area->format_version;
    hdr.header_size    = (uint16_t)sizeof(hdr);
    hdr.data_len       = len;
    hdr.data_crc       = crc;
    hdr.reserved       = 0u;

    /* 当前策略是整区重写，优先保证结构简单和恢复行为明确。 */
    /* 擦写flash 分区占用的 相关扇区 */
    rc                 = flash_partition_erase(&area->partition, area->offset, area->size);
    if (ret_is_err(rc)) return nv_storage_map_partition_rc(rc);

    /* 写入头部数据到 分区 */
    rc = flash_partition_write(&area->partition, area->offset, &hdr, (uint32_t)sizeof(hdr));
    if (ret_is_err(rc)) return nv_storage_map_partition_rc(rc);

    /* 写入负载数据到 nv */
    if (len != 0u) {
        rc = flash_partition_write(&area->partition, area->offset + (uint32_t)sizeof(hdr), data,
                                   len);
        if (ret_is_err(rc)) return nv_storage_map_partition_rc(rc);
    }

    return RET_OK;
}

ret_code_t nv_storage_erase(const char *name) {
    nv_storage_area_t *area = NULL;
    ret_code_t rc           = nv_storage_find(name, &area);
    if (ret_is_err(rc)) return rc;

    rc = flash_partition_erase(&area->partition, area->offset, area->size);
    return nv_storage_map_partition_rc(rc);
}

#else

ret_code_t nv_storage_init(const nv_storage_cfg_t *cfg) {
    (void)cfg;
    return NV_STORAGE_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t nv_storage_get(const char *name, nv_storage_area_t *out) {
    (void)name;
    (void)out;
    return NV_STORAGE_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t nv_storage_get_meta(const char *name, nv_storage_meta_t *out) {
    (void)name;
    (void)out;
    return NV_STORAGE_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t nv_storage_read(const char *name, void *buf, uint32_t buf_size, uint32_t *out_len) {
    (void)name;
    (void)buf;
    (void)buf_size;
    (void)out_len;
    return NV_STORAGE_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t nv_storage_write(const char *name, const void *data, uint32_t len) {
    (void)name;
    (void)data;
    (void)len;
    return NV_STORAGE_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t nv_storage_erase(const char *name) {
    (void)name;
    return NV_STORAGE_PARAM(RET_R_UNSUPPORTED);
}

#endif
