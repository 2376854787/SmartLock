#ifndef HAL_PWR_H
#define HAL_PWR_H

#include <stdbool.h>
#include <stdint.h>

#include "ret_code_t.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 电源模块内部状态机。 */
typedef enum {
    HAL_PWR_STATUS_UNINIT = 0,
    HAL_PWR_STATUS_READY  = 1,
    HAL_PWR_STATUS_BUSY   = 2,
} hal_pwr_status_t;
/* 电源低功耗的模式 */
typedef enum {
    HAL_PWR_MODE_RUN = 0,  /* 正常运行态 */
    HAL_PWR_MODE_SLEEP,    /* 浅睡眠，可快速恢复 */
    HAL_PWR_MODE_STOP,     /* 深睡眠，通常需要恢复时钟树 */
    HAL_PWR_MODE_STANDBY,  /* 待机态，唤醒更接近复位 */
    HAL_PWR_MODE_SHUTDOWN, /* 最低功耗关闭态 */
    HAL_PWR_MODE_COUNT,
} hal_pwr_mode_t;

/* 模式进入方式。 */
typedef enum {
    HAL_PWR_MODE_ENTRY_WFI = 0,
    HAL_PWR_MODE_ENTRY_WFE = 1,
} hal_pwr_mode_entry_t;

/* 低功耗进入方式能力位图。 */
enum {
    HAL_PWR_MODE_ENTRY_CAP_NONE = 0u,
    HAL_PWR_MODE_ENTRY_CAP_WFI  = (1u << 0),
    HAL_PWR_MODE_ENTRY_CAP_WFE  = (1u << 1),
};

/* 模式使用的稳压器策略。 */
typedef enum {
    HAL_PWR_REGULATOR_MAIN      = 0,
    HAL_PWR_REGULATOR_LOW_POWER = 1,
} hal_pwr_regulator_t;

enum {
    HAL_PWR_REGULATOR_CAP_NONE      = 0u,
    HAL_PWR_REGULATOR_CAP_MAIN      = (1u << 0),
    HAL_PWR_REGULATOR_CAP_LOW_POWER = (1u << 1),
};

/* 单个模式的硬件进入配置。
 * 由 HAL 层持有，port 只消费，不承载系统策略。 */
typedef struct {
    hal_pwr_mode_entry_t entry;    /* 进入方式：WFI/WFE */
    hal_pwr_regulator_t regulator; /* 进入该模式时使用的稳压器策略 */
} hal_pwr_mode_cfg_t;

/* 复位原始标志位图。
 * 接近 AUTOSAR 的 ResetRawValue，用于保留平台底层诊断信息。 */
/* 唤醒源通用选项位图。当前平台未定义额外选项。 */
enum {
    HAL_PWR_RESET_RAW_NONE      = 0u,
    HAL_PWR_RESET_RAW_POWER_ON  = (1u << 0),
    HAL_PWR_RESET_RAW_PIN       = (1u << 1),
    HAL_PWR_RESET_RAW_SOFTWARE  = (1u << 2),
    HAL_PWR_RESET_RAW_IWDG      = (1u << 3),
    HAL_PWR_RESET_RAW_WWDG      = (1u << 4),
    HAL_PWR_RESET_RAW_STANDBY   = (1u << 5),
    HAL_PWR_RESET_RAW_BROWN_OUT = (1u << 6),
};

/* 归一化复位原因。
 * 规则：底层 raw flag 可能并存，这里按固定优先级折叠主原因：
 * BROWN_OUT > WATCHDOG > SOFTWARE > STANDBY_WAKEUP > PIN > POWER_ON > UNDEFINED
 * 如果需要无损诊断，必须同时读取 hal_pwr_get_reset_raw_value()。 */
typedef enum {
    HAL_PWR_RESET_REASON_UNDEFINED = 0,
    HAL_PWR_RESET_REASON_POWER_ON,
    HAL_PWR_RESET_REASON_PIN,
    HAL_PWR_RESET_REASON_SOFTWARE,
    HAL_PWR_RESET_REASON_WATCHDOG,
    HAL_PWR_RESET_REASON_STANDBY_WAKEUP,
    HAL_PWR_RESET_REASON_BROWN_OUT,
} hal_pwr_reset_reason_t;

/* 唤醒原因位图。 */
/* 唤醒源实例编号约定。 */
enum {
    HAL_PWR_WAKEUP_REASON_NONE       = 0u,
    HAL_PWR_WAKEUP_REASON_PIN        = (1u << 0),
    HAL_PWR_WAKEUP_REASON_RTC_ALARM  = (1u << 1),
    HAL_PWR_WAKEUP_REASON_RTC_WAKEUP = (1u << 2),
    HAL_PWR_WAKEUP_REASON_WDG        = (1u << 3),
    HAL_PWR_WAKEUP_REASON_RESET      = (1u << 4),
};
/* 唤醒源 */
typedef enum {
    HAL_PWR_WAKEUP_SOURCE_PIN = 0,    /* 外部唤醒引脚 */
    HAL_PWR_WAKEUP_SOURCE_RTC_ALARM,  /* RTC Alarm */
    HAL_PWR_WAKEUP_SOURCE_RTC_WAKEUP, /* RTC Wakeup Timer */
    HAL_PWR_WAKEUP_SOURCE_WDG,        /* 看门狗唤醒 */
    HAL_PWR_WAKEUP_SOURCE_COUNT,
} hal_pwr_wakeup_source_t;

enum {
    HAL_PWR_WAKEUP_SOURCE_OPT_NONE = 0u,
};
/* 唤醒源实例 */
enum {
    HAL_PWR_WAKEUP_SOURCE_INSTANCE_DEFAULT = 0u,
    HAL_PWR_WAKEUP_SOURCE_INSTANCE_1       = 1u,
};

#define HAL_PWR_MODE_MASK(mode_)         (1u << (uint32_t)(mode_))
#define HAL_PWR_WAKEUP_SOURCE_MASK(src_) (1u << (uint32_t)(src_))

/* 配置集中的 wakeup source 策略。索引由 source enum 隐式决定。 */
typedef struct {
    uint8_t instance;      /* 唤醒源实例号 */
    bool enable;           /* 是否启用该唤醒源 */
    uint32_t option_flags; /* 平台扩展选项位 */
} hal_pwr_wakeup_source_policy_t;

/* 运行期对单个 wakeup source 的显式配置命令。 */
typedef struct {
    hal_pwr_wakeup_source_t source; /* 唤醒源类型 */
    uint8_t instance;               /* 唤醒源实例号 */
    bool enable;                    /* 是否启用 */
    uint32_t option_flags;          /* 平台扩展选项位 */
} hal_pwr_wakeup_source_cfg_t;

/* 单个配置集。用于表达不同功耗/唤醒策略组合。 */
typedef struct {
    uint8_t config_set_id;                           /* 配置集 id */
    bool allow_backup_access;                        /* 是否允许备份域访问 */
    bool clear_wakeup_flags_on_init;                 /* 应用配置集时是否清唤醒标志 */
    bool clear_standby_flag_on_init;                 /* 应用配置集时是否清 standby 标志 */
    hal_pwr_mode_cfg_t mode_cfg[HAL_PWR_MODE_COUNT]; /* 各功耗模式的硬件进入配置 */
    hal_pwr_wakeup_source_policy_t
        wakeup_source_cfg[HAL_PWR_WAKEUP_SOURCE_COUNT]; /* 唤醒源策略表 */
} hal_pwr_config_set_t;

/* 平台能力。 */
typedef struct {
    uint32_t supported_modes_mask;               /* 支持的功耗模式位图 */
    uint32_t configurable_wakeup_source_mask;    /* 可配置唤醒源位图 */
    uint32_t observable_wakeup_reason_mask;      /* 可观测唤醒原因位图 */
    uint32_t mode_entry_cap[HAL_PWR_MODE_COUNT]; /* 每种模式支持的进入方式能力位图 */
    uint32_t regulator_cap[HAL_PWR_MODE_COUNT];  /* 每种模式支持的稳压器能力位图 */
} hal_pwr_capability_t;

/* HAL PWR 根配置。
 * config_set 由上层以静态配置形式提供；HAL 只选择并应用其中之一。 */
typedef struct {
    const hal_pwr_config_set_t *config_sets; /* 配置集数组 */
    uint8_t config_set_count;                /* 配置集数量 */
    uint8_t default_config_set_id;           /* 默认启用的配置集 id */
} hal_pwr_cfg_t;

ret_code_t hal_pwr_init(const hal_pwr_cfg_t *cfg);
ret_code_t hal_pwr_deinit(void);
ret_code_t hal_pwr_init_check(const hal_pwr_cfg_t *cfg, bool *out_is_match);
ret_code_t hal_pwr_get_status(hal_pwr_status_t *out);
ret_code_t hal_pwr_get_capability(hal_pwr_capability_t *out);
ret_code_t hal_pwr_select_config_set(uint8_t config_set_id);
ret_code_t hal_pwr_get_active_config_set(uint8_t *out_config_set_id);
ret_code_t hal_pwr_get_mode(hal_pwr_mode_t *out_mode);
ret_code_t hal_pwr_set_mode(hal_pwr_mode_t mode);
ret_code_t hal_pwr_configure_wakeup_source(const hal_pwr_wakeup_source_cfg_t *cfg);
ret_code_t hal_pwr_enable_wakeup_source(hal_pwr_wakeup_source_t source);
ret_code_t hal_pwr_disable_wakeup_source(hal_pwr_wakeup_source_t source);
ret_code_t hal_pwr_get_wakeup_source_cfg(hal_pwr_wakeup_source_t source,
                                         hal_pwr_wakeup_source_cfg_t *out_cfg);
/* 获取复位原因 */
ret_code_t hal_pwr_get_reset_reason(hal_pwr_reset_reason_t *out_reason);
ret_code_t hal_pwr_get_reset_raw_value(uint32_t *out_raw_value);
/* 清理复位标志位 */
ret_code_t hal_pwr_clear_reset_flags(void);
/* 获取唤醒原因 */
ret_code_t hal_pwr_get_wakeup_reason(uint32_t *out_mask);
ret_code_t hal_pwr_clear_wakeup_reason(uint32_t mask);

void hal_pwr_on_port_error(ret_code_t rc_port, ret_code_t rc_hal, const char *api, uint32_t arg0,
                           uint32_t arg1);

#ifdef __cplusplus
}
#endif

#endif /* HAL_PWR_H */
