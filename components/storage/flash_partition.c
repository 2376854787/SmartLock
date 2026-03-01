#include "flash_partition.h"

#include "APP_config.h"

#define FLASH_PART_PARAM(reason_) RET_MAKE_PARAM(RET_MOD_STOR, RET_SUB_STOR_PART, (reason_))
#define FLASH_PART_STATE(reason_) RET_MAKE_STATE(RET_MOD_STOR, RET_SUB_STOR_PART, (reason_))
#define FLASH_PART_RES(reason_)   RET_MAKE_RESOURCE(RET_MOD_STOR, RET_SUB_STOR_PART, (reason_))
#define FLASH_PART_IO(reason_)    RET_MAKE_IO(RET_MOD_STOR, RET_SUB_STOR_PART, (reason_))

#if defined(CFG_FEAT_FLASH_PARTITION) && (CFG_FEAT_FLASH_PARTITION == 1)

#include <string.h>

#include "assert_cus.h"
#include "hal_flash.h"

#ifndef CFG_PARAM_FLASH_PARTITION_MAX
#define CFG_PARAM_FLASH_PARTITION_MAX 16u
#endif

typedef struct {
    bool initialized;                                         /* 初始化标志位 */
    uint32_t count;                                           /* 分区数量 */
    flash_partition_t entries[CFG_PARAM_FLASH_PARTITION_MAX]; /* 记录分区 */
} flash_partition_ctrl_t;
/* 全局 分区记录变量 */
static flash_partition_ctrl_t s_part;

static ret_code_t flash_partition_map_rc(ret_code_t rc) {
    if (ret_is_ok(rc)) return RET_OK;

    if (ret_is_class(rc, RET_CLASS_PARAM)) {
        if (ret_is_reason(rc, RET_R_NULL_PTR)) return FLASH_PART_PARAM(RET_R_NULL_PTR);
        if (ret_is_reason(rc, RET_R_RANGE_ERR)) return FLASH_PART_PARAM(RET_R_RANGE_ERR);
        if (ret_is_reason(rc, RET_R_UNSUPPORTED)) return FLASH_PART_PARAM(RET_R_UNSUPPORTED);
        return FLASH_PART_PARAM(RET_R_INVALID_ARG);
    }
    if (ret_is_class(rc, RET_CLASS_STATE)) {
        if (ret_is_reason(rc, RET_R_BUSY)) return FLASH_PART_STATE(RET_R_BUSY);
        if (ret_is_reason(rc, RET_R_NOT_READY)) return FLASH_PART_STATE(RET_R_NOT_READY);
        return FLASH_PART_STATE(RET_R_STATE_ERR);
    }
    if (ret_is_class(rc, RET_CLASS_RESOURCE)) {
        if (ret_is_reason(rc, RET_R_NO_MEM)) return FLASH_PART_RES(RET_R_NO_MEM);
        if (ret_is_reason(rc, RET_R_BUFFER_FULL)) return FLASH_PART_RES(RET_R_BUFFER_FULL);
        return FLASH_PART_RES(RET_R_NO_RESOURCE);
    }
    if (ret_is_class(rc, RET_CLASS_TIMEOUT))
        return RET_MAKE_TIMEOUT(RET_MOD_STOR, RET_SUB_STOR_PART, RET_R_TIMEOUT);
    return FLASH_PART_IO(RET_R_FLASH_ERR);
}

static bool flash_partition_name_valid(const char *name) {
    return (name != NULL) && (name[0] != '\0');
}
/**
 * @brief 输入基地址和分区内部偏移量以及长度 返回绝对地址
 * @param part 分区信息
 * @param offset 偏移量
 * @param len 长度
 * @param abs_addr 返回绝对地址
 * @return 状态码
 */
static ret_code_t flash_partition_check_access(const flash_partition_t *part, uint32_t offset,
                                               uint32_t len, uint32_t *abs_addr) {
    REQUIRE_RET(s_part.initialized, FLASH_PART_STATE(RET_R_NOT_READY));
    REQUIRE_RET(part != NULL, FLASH_PART_PARAM(RET_R_NULL_PTR));
    REQUIRE_RET((part->name != NULL) && (part->size != 0u), FLASH_PART_PARAM(RET_R_INVALID_ARG));
    REQUIRE_RET(len != 0u, FLASH_PART_PARAM(RET_R_RANGE_ERR));
    REQUIRE_RET(offset < part->size, FLASH_PART_PARAM(RET_R_RANGE_ERR));
    REQUIRE_RET(len <= (part->size - offset), FLASH_PART_PARAM(RET_R_RANGE_ERR));

    if (abs_addr != NULL) *abs_addr = part->base + offset;
    return RET_OK;
}

static ret_code_t flash_partition_find(const char *name, flash_partition_t **out) {
    REQUIRE_RET(s_part.initialized, FLASH_PART_STATE(RET_R_NOT_READY));
    REQUIRE_RET(name != NULL, FLASH_PART_PARAM(RET_R_NULL_PTR));
    REQUIRE_RET(out != NULL, FLASH_PART_PARAM(RET_R_NULL_PTR));

    for (uint32_t i = 0; i < s_part.count; ++i) {
        if (strcmp(s_part.entries[i].name, name) == 0) {
            *out = &s_part.entries[i];
            return RET_OK;
        }
    }
    return FLASH_PART_PARAM(RET_R_RANGE_ERR);
}

ret_code_t flash_partition_init(const flash_partition_cfg_t *cfg) {
    /*　flash 信息 */
    hal_flash_info_t flash_info = {0};
    /* 参数检查 */
    REQUIRE_RET(cfg != NULL, FLASH_PART_PARAM(RET_R_NULL_PTR));
    REQUIRE_RET(cfg->entries != NULL, FLASH_PART_PARAM(RET_R_NULL_PTR));
    REQUIRE_RET((cfg->count != 0u) && (cfg->count <= CFG_PARAM_FLASH_PARTITION_MAX),
                FLASH_PART_PARAM(RET_R_RANGE_ERR));
    REQUIRE_RET(!s_part.initialized, FLASH_PART_STATE(RET_R_BUSY));

    /* 获取 Flash 信息 */
    const ret_code_t rc = hal_flash_get_info(&flash_info);
    if (ret_is_err(rc)) return flash_partition_map_rc(rc);

    memset(&s_part, 0, sizeof(s_part));

    /* 初始化阶段把相对偏移固化成绝对地址，运行时避免重复换算。 */
    for (uint32_t i = 0; i < cfg->count; ++i) {
        /* 遍历分区 */
        const flash_partition_entry_t *entry = &cfg->entries[i];
        /* 检查参数 */
        REQUIRE_RET(flash_partition_name_valid(entry->name), FLASH_PART_PARAM(RET_R_INVALID_ARG));
        REQUIRE_RET(entry->size != 0u, FLASH_PART_PARAM(RET_R_RANGE_ERR));
        REQUIRE_RET(entry->offset <= flash_info.total_size, FLASH_PART_PARAM(RET_R_RANGE_ERR));
        REQUIRE_RET(entry->size <= (flash_info.total_size - entry->offset),
                    FLASH_PART_PARAM(RET_R_RANGE_ERR));
        /* 往全局资源添加 分区信息 */
        s_part.entries[i].name  = entry->name;
        s_part.entries[i].base  = flash_info.base + entry->offset;
        s_part.entries[i].size  = entry->size;
        s_part.entries[i].flags = entry->flags;
    }

    /* 分区表必须名称唯一且地址不重叠。 */
    for (uint32_t i = 0; i < cfg->count; ++i) {
        const uint32_t a_begin = s_part.entries[i].base;
        const uint32_t a_end   = a_begin + s_part.entries[i].size;
        for (uint32_t j = i + 1u; j < cfg->count; ++j) {
            const uint32_t b_begin = s_part.entries[j].base;
            const uint32_t b_end   = b_begin + s_part.entries[j].size;
            REQUIRE_RET(strcmp(s_part.entries[i].name, s_part.entries[j].name) != 0,
                        FLASH_PART_PARAM(RET_R_INVALID_ARG));
            REQUIRE_RET((a_end <= b_begin) || (b_end <= a_begin),
                        FLASH_PART_PARAM(RET_R_STATE_ERR));
        }
    }

    s_part.count       = cfg->count;
    s_part.initialized = true;
    return RET_OK;
}

ret_code_t flash_partition_get_count(uint32_t *out) {
    REQUIRE_RET(out != NULL, FLASH_PART_PARAM(RET_R_NULL_PTR));
    REQUIRE_RET(s_part.initialized, FLASH_PART_STATE(RET_R_NOT_READY));
    *out = s_part.count;
    return RET_OK;
}

ret_code_t flash_partition_get_by_index(uint32_t index, flash_partition_t *out) {
    REQUIRE_RET(out != NULL, FLASH_PART_PARAM(RET_R_NULL_PTR));
    REQUIRE_RET(s_part.initialized, FLASH_PART_STATE(RET_R_NOT_READY));
    REQUIRE_RET(index < s_part.count, FLASH_PART_PARAM(RET_R_RANGE_ERR));
    *out = s_part.entries[index];
    return RET_OK;
}

ret_code_t flash_partition_get(const char *name, flash_partition_t *out) {
    flash_partition_t *part = NULL;

    REQUIRE_RET(out != NULL, FLASH_PART_PARAM(RET_R_NULL_PTR));
    const ret_code_t rc = flash_partition_find(name, &part);
    if (ret_is_err(rc)) return rc;
    *out = *part;
    return RET_OK;
}

ret_code_t flash_partition_read(const flash_partition_t *part, uint32_t offset, void *dst,
                                uint32_t len) {
    uint32_t abs_addr = 0u;
    ret_code_t rc     = flash_partition_check_access(part, offset, len, &abs_addr);
    if (ret_is_err(rc)) return rc;
    REQUIRE_RET(dst != NULL, FLASH_PART_PARAM(RET_R_NULL_PTR));

    rc = hal_flash_read_sync(abs_addr, dst, len);
    return flash_partition_map_rc(rc);
}

ret_code_t flash_partition_write(const flash_partition_t *part, uint32_t offset, const void *src,
                                 uint32_t len) {
    uint32_t abs_addr = 0u;
    ret_code_t rc     = flash_partition_check_access(part, offset, len, &abs_addr);
    if (ret_is_err(rc)) return rc;
    REQUIRE_RET(src != NULL, FLASH_PART_PARAM(RET_R_NULL_PTR));
    REQUIRE_RET((part->flags & FLASH_PARTITION_FLAG_READONLY) == 0u,
                FLASH_PART_STATE(RET_R_STATE_ERR));

    rc = hal_flash_write_sync(abs_addr, src, len);
    return flash_partition_map_rc(rc);
}

ret_code_t flash_partition_erase(const flash_partition_t *part, uint32_t offset, uint32_t len) {
    uint32_t abs_addr = 0u;
    ret_code_t rc     = flash_partition_check_access(part, offset, len, &abs_addr);
    if (ret_is_err(rc)) return rc;
    REQUIRE_RET((part->flags & FLASH_PARTITION_FLAG_READONLY) == 0u,
                FLASH_PART_STATE(RET_R_STATE_ERR));

    rc = hal_flash_erase_sync(abs_addr, len);
    return flash_partition_map_rc(rc);
}

ret_code_t flash_partition_blank_check(const flash_partition_t *part, uint32_t offset, uint32_t len,
                                       bool *out) {
    uint32_t abs_addr = 0u;
    ret_code_t rc     = flash_partition_check_access(part, offset, len, &abs_addr);
    if (ret_is_err(rc)) return rc;
    REQUIRE_RET(out != NULL, FLASH_PART_PARAM(RET_R_NULL_PTR));

    rc = hal_flash_blank_check_sync(abs_addr, len, out);
    return flash_partition_map_rc(rc);
}

#else

ret_code_t flash_partition_init(const flash_partition_cfg_t *cfg) {
    (void)cfg;
    return FLASH_PART_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t flash_partition_get_count(uint32_t *out) {
    (void)out;
    return FLASH_PART_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t flash_partition_get_by_index(uint32_t index, flash_partition_t *out) {
    (void)index;
    (void)out;
    return FLASH_PART_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t flash_partition_get(const char *name, flash_partition_t *out) {
    (void)name;
    (void)out;
    return FLASH_PART_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t flash_partition_read(const flash_partition_t *part, uint32_t offset, void *dst,
                                uint32_t len) {
    (void)part;
    (void)offset;
    (void)dst;
    (void)len;
    return FLASH_PART_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t flash_partition_write(const flash_partition_t *part, uint32_t offset, const void *src,
                                 uint32_t len) {
    (void)part;
    (void)offset;
    (void)src;
    (void)len;
    return FLASH_PART_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t flash_partition_erase(const flash_partition_t *part, uint32_t offset, uint32_t len) {
    (void)part;
    (void)offset;
    (void)len;
    return FLASH_PART_PARAM(RET_R_UNSUPPORTED);
}

ret_code_t flash_partition_blank_check(const flash_partition_t *part, uint32_t offset, uint32_t len,
                                       bool *out) {
    (void)part;
    (void)offset;
    (void)len;
    (void)out;
    return FLASH_PART_PARAM(RET_R_UNSUPPORTED);
}

#endif
