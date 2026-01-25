#ifndef SMARTLOCK_RET_CODE_H
#define SMARTLOCK_RET_CODE_H

#include <stdbool.h>
#include <stdint.h>

#include "compiler_cus.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * 商业级 32-bit 状态码（Layer/Module + Component/Submodule + Code）
 *
 * 位布局：
 *   [31:24] Module (Layer)      8-bit
 *   [23:16] Submodule(Component)8-bit
 *   [15:0 ] Code                16-bit  (Class + Reason)
 *
 * Code(16-bit) ：
 *   [15:12] Class   4-bit  -> 错误大类（用于上层策略/统计聚类）
 *   [11:0 ] Reason 12-bit  -> 具体原因（用于细粒度定位）
 *
 * 约定：
 *   - RET_OK 必须为 0
 *   - 上层逻辑禁止写死 rc == 某个固定值；应使用 ret_is_xxx / ret_class 等判断
 *   - 发布后：Module/Submodule/Reason 值尽量保持稳定，只新增不改旧值
 * =============================================================================
 */

typedef uint32_t ret_code_t;

/* =============================================================================
 * 1) Module / Layer（高 8 位）
 * =============================================================================
 */
typedef enum {
    RET_MOD_NONE  = 0x00u, /* 保留 */
    RET_MOD_CORE  = 0x01u, /* core_base / 通用核心 */
    RET_MOD_OSAL  = 0x02u, /* OS 抽象层 */
    RET_MOD_HAL   = 0x03u, /* HAL 抽象层 */
    RET_MOD_RB    = 0x04u, /* RingBuffer */
    RET_MOD_MEM   = 0x05u, /* MemPool / MemoryAllocation */
    RET_MOD_LOG   = 0x06u, /* 日志 */
    RET_MOD_AT    = 0x07u, /* AT/协议栈 */
    RET_MOD_PORT  = 0x08u, /* 平台端口层 platform/ports */
    RET_MOD_APP   = 0x09u, /* 业务应用层（SmartLock app） */
    RET_MOD_SEC   = 0x0Au, /* 安全/加密/证书 */
    RET_MOD_STOR  = 0x0Bu, /* 存储（Flash KV/FS/参数区） */
    RET_MOD_OTA   = 0x0Cu, /* OTA/Boot/升级 */
    RET_MOD_TOOLS = 0x0Du, /* 通用工具类 */
} ret_module_id_t;

/* =============================================================================
 * 2) Submodule / Component（中 8 位）
 *
 * 约束建议：
 *   - 0x00：该模块通用/未细分（兼容与兜底）
 *   - 0x01~0x7F：该模块内部子模块
 *   - 0x80~0xFF：预留给项目/板级/客户定制扩展
 * =============================================================================
 */

/* ---- CORE 子模块 ---- */
typedef enum {
    RET_SUB_CORE_NONE   = 0x00u,
    RET_SUB_CORE_UTIL   = 0x01u, /* crc/bit/math */
    RET_SUB_CORE_ASSERT = 0x02u, /* assert/fault record */
    RET_SUB_CORE_CFG    = 0x03u, /* build cfg/static check */
} ret_sub_core_t;

/* ---- OSAL 子模块 ---- */
typedef enum {
    RET_SUB_OSAL_NONE  = 0x00u,
    RET_SUB_OSAL_TASK  = 0x01u,
    RET_SUB_OSAL_QUEUE = 0x02u,
    RET_SUB_OSAL_SEM   = 0x03u,
    RET_SUB_OSAL_MUTEX = 0x04u,
    RET_SUB_OSAL_TIMER = 0x05u,
    RET_SUB_OSAL_ISR   = 0x06u,
} ret_sub_osal_t;

/* ---- HAL 子模块 ---- */
typedef enum {
    RET_SUB_HAL_NONE  = 0x00u,
    RET_SUB_HAL_GPIO  = 0x01u,
    RET_SUB_HAL_UART  = 0x02u,
    RET_SUB_HAL_I2C   = 0x03u,
    RET_SUB_HAL_SPI   = 0x04u,
    RET_SUB_HAL_ADC   = 0x05u,
    RET_SUB_HAL_DMA   = 0x06u,
    RET_SUB_HAL_TIM   = 0x07u,
    RET_SUB_HAL_FLASH = 0x08u,
    RET_SUB_HAL_RTC   = 0x09u,
    RET_SUB_HAL_WDT   = 0x0Au,
    RET_SUB_HAL_PWR   = 0x0Bu,
    RET_SUB_HAL_EXTI  = 0x0Cu,
} ret_sub_hal_t;

/* ---- RingBuffer 子模块 ---- */
typedef enum {
    RET_SUB_RB_NONE   = 0x00u,
    RET_SUB_RB_CORE   = 0x01u,
    RET_SUB_RB_ZC     = 0x02u, /* zero-copy reserve/commit */
    RET_SUB_RB_DMA_RX = 0x03u,
    RET_SUB_RB_DMA_TX = 0x04u,
} ret_sub_rb_t;

/* ---- MEM 子模块 ---- */
typedef enum {
    RET_SUB_MEM_NONE  = 0x00u,
    RET_SUB_MEM_POOL  = 0x01u,
    RET_SUB_MEM_SLAB  = 0x02u,
    RET_SUB_MEM_GUARD = 0x03u,
} ret_sub_mem_t;

/* ---- LOG 子模块 ---- */
typedef enum {
    RET_SUB_LOG_NONE    = 0x00u,
    RET_SUB_LOG_CORE    = 0x01u,
    RET_SUB_LOG_BACKEND = 0x02u, /* RTT/UART/Flash */
    RET_SUB_LOG_FMT     = 0x03u,
} ret_sub_log_t;

/* ---- AT 子模块 ---- */
typedef enum {
    RET_SUB_AT_NONE      = 0x00u,
    RET_SUB_AT_PARSER    = 0x01u,
    RET_SUB_AT_DISPATCH  = 0x02u,
    RET_SUB_AT_TRANSPORT = 0x03u,
    RET_SUB_AT_SESSION   = 0x04u,
    RET_SUB_AT_MQTT      = 0x05u,
    RET_SUB_AT_HTTP      = 0x06u,
} ret_sub_at_t;

/* ---- PORT 子模块 ---- */
typedef enum {
    RET_SUB_PORT_NONE   = 0x00u,
    RET_SUB_PORT_STM32  = 0x01u,
    RET_SUB_PORT_ESP    = 0x02u,
    RET_SUB_PORT_LINUX  = 0x03u,
    RET_SUB_PORT_DRIVER = 0x04u,
} ret_sub_port_t;

/* ---- APP 子模块---- */
typedef enum {
    RET_SUB_APP_NONE    = 0x00u,
    RET_SUB_APP_LOCK    = 0x01u, /* 锁控状态机 */
    RET_SUB_APP_SENSOR  = 0x02u, /* 传感器/门磁 */
    RET_SUB_APP_UI      = 0x03u, /* 屏幕/按键 */
    RET_SUB_APP_NET     = 0x04u, /* 联网业务（若有） */
    RET_SUB_APP_STORAGE = 0x05u, /* 参数/记录（若有） */
} ret_sub_app_t;

/* ---- SEC 子模块 ---- */
typedef enum {
    RET_SUB_SEC_NONE   = 0x00u,
    RET_SUB_SEC_CRYPTO = 0x01u,
    RET_SUB_SEC_CERT   = 0x02u,
    RET_SUB_SEC_TLS    = 0x03u,
} ret_sub_sec_t;

/* ---- STOR 子模块 ---- */
typedef enum {
    RET_SUB_STOR_NONE  = 0x00u,
    RET_SUB_STOR_KV    = 0x01u,
    RET_SUB_STOR_FS    = 0x02u,
    RET_SUB_STOR_PARAM = 0x03u,
} ret_sub_stor_t;

/* ---- OTA 子模块 ---- */
typedef enum {
    RET_SUB_OTA_NONE   = 0x00u,
    RET_SUB_OTA_BOOT   = 0x01u,
    RET_SUB_OTA_SWAP   = 0x02u,
    RET_SUB_OTA_IMAGE  = 0x03u,
    RET_SUB_OTA_VERIFY = 0x04u,
} ret_sub_ota_t;
/* ---- 通用工具 子模块 ---- */
typedef enum {
    RET_SUB_TOOLS_NONE = 0x00u,
    RET_SUB_TOOLS_CRC  = 0x01u,
} ret_sub_tools_t;
/* =============================================================================
 * 3) Code(16-bit) = Class(4-bit) + Reason(12-bit)
 * =============================================================================
 */

/* ---- Class：错误大类（上层策略/统计用） ---- */
typedef enum {
    RET_CLASS_OK       = 0x0u,
    RET_CLASS_PARAM    = 0x1u, /* 参数/用法错误 */
    RET_CLASS_STATE    = 0x2u, /* 状态/时序错误（not_ready/busy） */
    RET_CLASS_TIMEOUT  = 0x3u, /* 超时 */
    RET_CLASS_RESOURCE = 0x4u, /* 资源不足（内存/句柄/队列满） */
    RET_CLASS_IO       = 0x5u, /* 外设/IO */
    RET_CLASS_DATA     = 0x6u, /* 数据/协议/校验 */
    RET_CLASS_SECURITY = 0x7u, /* 安全/鉴权 */
    RET_CLASS_FATAL    = 0xFu, /* 致命（不可恢复） */
} ret_class_t;

/* ---- Reason：通用原因（12-bit） ----
 * 说明：Reason 是“跨模块通用”的原因表
 */
typedef enum {
    RET_R_OK              = 0x000u,

    /* PARAM (0x1xx) */
    RET_R_INVALID_ARG     = 0x001u, /* 参数非法：值不合法/枚举越界/格式不符 */
    RET_R_NULL_PTR        = 0x002u, /* 空指针：传入指针参数为 NULL */
    RET_R_RANGE_ERR       = 0x003u, /* 范围错误：数值超过允许范围（如阈值/长度/索引） */
    RET_R_ALIGN_ERR       = 0x004u, /* 对齐错误：地址/长度不满足对齐要求（DMA/总线常见） */
    RET_R_UNSUPPORTED     = 0x005u, /* 不支持：功能/模式/平台不支持（编译期或运行期拒绝） */

    /* STATE (0x2xx) */
    RET_R_FAIL            = 0x010u, /* 未分类失败：兜底错误（能细分就不要用它） */
    RET_R_NOT_READY       = 0x011u, /* 未就绪：初始化未完成/依赖未满足/设备未准备好 */
    RET_R_BUSY            = 0x012u, /* 忙：资源占用/正在传输/锁已持有（建议退避重试） */
    RET_R_STATE_ERR       = 0x013u, /* 状态机错误：状态不一致/非法状态迁移/重复调用顺序错 */
    RET_R_WOULD_BLOCK     = 0x014u, /* 将阻塞：非阻塞 API 语义下无法立即完成（如无数据/无空间） */

    /* TIMEOUT (0x3xx) */
    RET_R_TIMEOUT         = 0x020u, /* 超时：等待事件/外设响应/队列/信号量超过设定时间 */

    /* RESOURCE (0x4xx) */
    RET_R_NO_MEM          = 0x030u, /* 内存不足：内存池耗尽/无法分配块 */
    RET_R_NO_RESOURCE     = 0x031u, /* 资源不足：句柄/描述符/对象槽位耗尽（非内存意义） */
    RET_R_QUEUE_FULL      = 0x032u, /* 队列满：OSAL queue 满（生产速度 > 消费速度） */
    RET_R_BUFFER_FULL     = 0x033u, /* 缓冲满：RingBuffer/Tx buffer 满（写入空间不足） */

    /* IO (0x5xx) */
    RET_R_IO              = 0x040u, /* IO 错误：读写失败/总线错误/返回码异常（通用） */
    RET_R_HW_FAULT        = 0x041u, /* 硬件故障：外设卡死/线路异常/供电异常推断 */
    RET_R_DMA_ERR         = 0x042u, /* DMA 错误：DMA 传输错误/配置不当/回调异常 */
    RET_R_FLASH_ERR       = 0x043u, /* Flash 错误：擦写失败/校验失败/越界访问等 */

    /* DATA (0x6xx) */
    RET_R_CRC             = 0x050u, /* CRC 错误：CRC 校验失败（通讯帧/镜像常见） */
    RET_R_CHECKSUM        = 0x051u, /* 校验和错误：非 CRC 的 checksum/累加和失败 */
    RET_R_DATA_OVERFLOW   = 0x052u, /* 数据溢出：长度超限/解析写爆/计数溢出 */
    RET_R_DATA_NOT_ENOUGH = 0x053u, /* 数据不足：解析需要更多字节（常用于流式解析器） */
    RET_R_PARSE_ERR       = 0x054u, /* 解析错误：格式不合法/字段不符合协议规范 */

    /* SECURITY (0x7xx) */
    RET_R_AUTH_FAIL       = 0x060u, /* 鉴权失败：口令/签名/令牌不通过 */
    RET_R_CERT_INVALID    = 0x061u, /* 证书无效：过期/链不完整/域名不匹配等 */
    RET_R_TLS_HANDSHAKE   = 0x062u, /* TLS 握手失败：协商失败/证书验证失败/加密套件不匹配 */

    /* FATAL (0xFxx) */
    RET_R_ISR_CONTEXT     = 0x070u, /* ISR 语境错误：禁止在中断里调用阻塞/非 FromISR API */
    RET_R_PANIC           = 0x071u, /* 致命错误：不可恢复（断言失败/关键一致性破坏等） */
} ret_reason_t;

/* =============================================================================
 * 4) 打包/解包宏
 * =============================================================================
 */

#define RET_OK ((ret_code_t)0u)

/* Code = (class<<12) | (reason & 0x0FFF) */
#define RET_CODE_MAKE(cls_, reason_) \
    ((uint16_t)((((uint16_t)(cls_) & 0x0Fu) << 12) | ((uint16_t)(reason_) & 0x0FFFu)))

/* ret_code = (mod<<24) | (sub<<16) | code */
#define RET_MAKE(mod_, sub_, code16_)                                                       \
    ((ret_code_t)((((uint32_t)(mod_) & 0xFFu) << 24) | (((uint32_t)(sub_) & 0xFFu) << 16) | \
                  ((uint32_t)(code16_) & 0xFFFFu)))

/* 解包 */
#define RET_MODULE(rc_)    ((uint8_t)((((uint32_t)(rc_)) >> 24) & 0xFFu))
#define RET_SUBMODULE(rc_) ((uint8_t)((((uint32_t)(rc_)) >> 16) & 0xFFu))
#define RET_CODE16(rc_)    ((uint16_t)(((uint32_t)(rc_)) & 0xFFFFu))
#define RET_CLASS(rc_)     ((ret_class_t)((RET_CODE16(rc_) >> 12) & 0x0Fu))
#define RET_REASON(rc_)    ((uint16_t)(RET_CODE16(rc_) & 0x0FFFu))

/* =============================================================================
 * 5) 通用构造宏 减少手写错误
 * =============================================================================
 */

/* 常用 class 构造（reason 用 ret_reason_t） */
#define RET_MAKE_PARAM(mod_, sub_, reason_) \
    RET_MAKE((mod_), (sub_), RET_CODE_MAKE(RET_CLASS_PARAM, (reason_)))
#define RET_MAKE_STATE(mod_, sub_, reason_) \
    RET_MAKE((mod_), (sub_), RET_CODE_MAKE(RET_CLASS_STATE, (reason_)))
#define RET_MAKE_TIMEOUT(mod_, sub_, reason_) \
    RET_MAKE((mod_), (sub_), RET_CODE_MAKE(RET_CLASS_TIMEOUT, (reason_)))
#define RET_MAKE_RESOURCE(mod_, sub_, reason_) \
    RET_MAKE((mod_), (sub_), RET_CODE_MAKE(RET_CLASS_RESOURCE, (reason_)))
#define RET_MAKE_IO(mod_, sub_, reason_) \
    RET_MAKE((mod_), (sub_), RET_CODE_MAKE(RET_CLASS_IO, (reason_)))
#define RET_MAKE_DATA(mod_, sub_, reason_) \
    RET_MAKE((mod_), (sub_), RET_CODE_MAKE(RET_CLASS_DATA, (reason_)))
#define RET_MAKE_SEC(mod_, sub_, reason_) \
    RET_MAKE((mod_), (sub_), RET_CODE_MAKE(RET_CLASS_SECURITY, (reason_)))
#define RET_MAKE_FATAL(mod_, sub_, reason_) \
    RET_MAKE((mod_), (sub_), RET_CODE_MAKE(RET_CLASS_FATAL, (reason_)))

/* 模块快捷宏 */
#define RET_HAL_UART_TIMEOUT() RET_MAKE_TIMEOUT(RET_MOD_HAL, RET_SUB_HAL_UART, RET_R_TIMEOUT)
#define RET_HAL_I2C_TIMEOUT()  RET_MAKE_TIMEOUT(RET_MOD_HAL, RET_SUB_HAL_I2C, RET_R_TIMEOUT)
#define RET_MEM_NO_MEM()       RET_MAKE_RESOURCE(RET_MOD_MEM, RET_SUB_MEM_POOL, RET_R_NO_MEM)
#define RET_AT_PARSE_ERR()     RET_MAKE_DATA(RET_MOD_AT, RET_SUB_AT_PARSER, RET_R_PARSE_ERR)

/* =============================================================================
 * 6) 判断函数
 * =============================================================================
 */

CORE_INLINE bool ret_is_ok(ret_code_t r) {
    return (r == RET_OK);
}
CORE_INLINE bool ret_is_err(ret_code_t r) {
    return (r != RET_OK);
}

CORE_INLINE bool ret_is_module(ret_code_t r, uint8_t mod) {
    return (RET_MODULE(r) == mod);
}
CORE_INLINE bool ret_is_submodule(ret_code_t r, uint8_t sub) {
    return (RET_SUBMODULE(r) == sub);
}
CORE_INLINE bool ret_is_class(ret_code_t r, ret_class_t cls) {
    return (RET_CLASS(r) == cls);
}
CORE_INLINE bool ret_is_reason(ret_code_t r, uint16_t reason12) {
    return (RET_REASON(r) == (reason12 & 0x0FFFu));
}

/* 常用分类判断 */
CORE_INLINE bool ret_is_timeout(ret_code_t r) {
    return ret_is_class(r, RET_CLASS_TIMEOUT);
}
CORE_INLINE bool ret_is_busy(ret_code_t r) {
    return ret_is_reason(r, RET_R_BUSY);
}
CORE_INLINE bool ret_is_no_mem(ret_code_t r) {
    return ret_is_reason(r, RET_R_NO_MEM);
}
CORE_INLINE bool ret_is_invalid_arg(ret_code_t r) {
    return ret_is_reason(r, RET_R_INVALID_ARG);
}

#ifdef __cplusplus
}
#endif

#endif /* SMARTLOCK_RET_CODE_H */
