#ifndef APP_AS608_SERVICE_H
#define APP_AS608_SERVICE_H

#include <stdint.h>
#include <stdbool.h>

#include "driver_as608.h"   /* as608_status_t 等 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * AS608 CRUD 服务（CMSIS-RTOS2 任务化封装）
 *
 * 目标：
 *  - 单线程独占访问 AS608（避免多任务并发导致帧交织）
 *  - 上层通过同步 API 发起 CRUD 请求：Create(录入)、Read(搜索/验证)、Update(覆盖录入)、Delete(删除)
 *
 * 典型流程：
 *  - AS608_Service_Init()：启动服务任务并完成 as608_init / verify_password
 *  - AS608_CRUD_Create(id) ：双次采集指纹并存入指定 id
 *  - AS608_CRUD_Read() ：搜索当前指纹，返回匹配的 id 和 score
 *  - AS608_CRUD_Update(id) ：先删除 id 再录入
 *  - AS608_CRUD_Delete(id) ：删除指定 id
 */

typedef enum
{
    AS608_SVC_OK = 0,
    AS608_SVC_ERR = 1,
    AS608_SVC_TIMEOUT = 2,
    AS608_SVC_NOT_READY = 3,
} as608_svc_rc_t;

/**
 * @brief 初始化 AS608 服务（创建队列/任务，并在任务内初始化模块）。
 * @param[in] addr  AS608 设备地址（典型默认 0xFFFFFFFF）
 * @param[in] password  校验密码（典型默认 0x00000000）
 */
as608_svc_rc_t AS608_Service_Init(uint32_t addr, uint32_t password);

/**
 * @brief Create：录入并存储到指定 id（page_number）。
 * @param[in] id  指纹库页号（0..capacity-1）
 * @param[in] timeout_ms  总超时（建议 15000~30000）
 */
as608_svc_rc_t AS608_CRUD_Create(uint16_t id, uint32_t timeout_ms, as608_status_t *out_status);

/**
 * @brief Read：搜索当前指纹，返回匹配的 id 和 score。
 */
as608_svc_rc_t AS608_CRUD_Read(uint32_t timeout_ms,
                               uint16_t *out_found_id,
                               uint16_t *out_score,
                               as608_status_t *out_status);

/**
 * @brief Update：覆盖录入指定 id（内部执行 delete + create）。
 */
as608_svc_rc_t AS608_CRUD_Update(uint16_t id, uint32_t timeout_ms, as608_status_t *out_status);

/**
 * @brief Delete：删除指定 id。
 */
as608_svc_rc_t AS608_CRUD_Delete(uint16_t id, as608_status_t *out_status);

/**
 * @brief Delete All：清空指纹库。
 */
as608_svc_rc_t AS608_CRUD_ClearAll(as608_status_t *out_status);

/**
 * @brief 获取索引表（用于“列举已有指纹”的基础能力）。
 * @param[in] num  0..3（每个表 32 字节，共覆盖 1024 bit；不同模块库大小可能 <1024）
 */
as608_svc_rc_t AS608_List_IndexTable(uint8_t num, uint8_t out_table[32], as608_status_t *out_status);

/**
 * @brief 获取模块容量（页数）。
 */
uint16_t AS608_Get_Capacity(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_AS608_SERVICE_H */
