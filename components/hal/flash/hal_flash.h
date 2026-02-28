#ifndef SMARTLOCK_HAL_FLASH_H
#define SMARTLOCK_HAL_FLASH_H

#include <stdbool.h>
#include <stdint.h>

#include "ret_code.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Flash HAL flash 模块状态语义。 */
typedef enum {
    HAL_FLASH_STATUS_UNINIT = 0,
    HAL_FLASH_STATUS_IDLE   = 1,
    HAL_FLASH_STATUS_BUSY   = 2,
} hal_flash_status_t;

/* 当前仅作为 HAL 内部调度提示，主要影响 Compare 的分块策略。 */
typedef enum {
    HAL_FLASH_MODE_SLOW = 0,
    HAL_FLASH_MODE_FAST = 1,
} hal_flash_mode_t;

/* 描述当前或最近一次作业的完成情况。 */
typedef enum {
    HAL_FLASH_JOB_NONE     = 0,
    HAL_FLASH_JOB_PENDING  = 1,
    /* 以下三选一 */
    HAL_FLASH_JOB_OK       = 2,
    HAL_FLASH_JOB_FAILED   = 3,
    HAL_FLASH_JOB_CANCELED = 4,
} hal_flash_job_result_t;

typedef struct {
    uint32_t base;                   /* Flash 基地址 */
    uint32_t total_size;             /* Flash 总容量（字节） */
    uint32_t prog_unit;              /* 最小编程粒度（字节） */
    uint32_t erase_value;            /* 擦除后字节值 */
    uint32_t min_erase_size;         /* 最小擦除粒度（字节，当前芯片为最小 sector 大小） */
    bool require_erase_before_write; /* 写前是否必须先擦除 */
} hal_flash_info_t;

typedef struct {
    uint32_t index; /* region/sector 编号，通常对应底层物理 sector 索引 */
    uint32_t addr;  /* region 起始地址，绝对 Flash 地址 */
    uint32_t size;  /* region 大小，单位字节 */
} hal_flash_region_t;

/* 对于 IT 驱动完成的 erase/write，通知回调可能运行在 ISR 上下文。 */
typedef void (*hal_flash_job_notify_t)(void *user, hal_flash_job_result_t result);

typedef struct {
    hal_flash_job_notify_t job_end_notify;   /* Job 成功完成通知，NULL=不注册 */
    hal_flash_job_notify_t job_error_notify; /* Job 失败/取消通知，NULL=不注册 */
    void *user;                              /* 回调透传用户上下文 */
    bool enable_background_worker;           /* RTOS 场景下是否自动创建后台 worker 推进 read/compare/blank_check job */
    uint32_t worker_stack_size;              /* 后台 worker 栈大小；0 表示使用模块默认值 */
} hal_flash_cfg_t;

/**
 * @brief 初始化 Flash 模块
 * @param cfg 模块配置；传 NULL 使用默认配置
 * @return RET_OK 或错误码
 * @note 模块为单例，重复初始化会返回 BUSY
 */
ret_code_t hal_flash_init(const hal_flash_cfg_t *cfg);

/**============================================================================================ */
/**==================================      GET/SET     ===================================== */
/**============================================================================================ */
/**
 * @brief 查询模块状态
 * @param out 输出状态
 * @return RET_OK 或错误码
 * @note 仅反映模块是否有 job 在执行，不区分具体 job 类型
 */
ret_code_t hal_flash_get_status(hal_flash_status_t *out);

/**
 * @brief 查询当前/最近一次 job 结果
 * @param out 输出 job 结果
 * @return RET_OK 或错误码
 * @note 当状态为 IDLE 时，该值表示最近一次 job 的结果
 */
ret_code_t hal_flash_get_job_result(hal_flash_job_result_t *out);

/**
 * @brief 设置 Flash 作业模式
 * @param mode 模式
 * @return RET_OK 或错误码
 * @note 当前实现主要影响比较类作业的分块策略；port 侧擦写仍可能忽略该提示 必须f lash 空闲状态下切换
 */
ret_code_t hal_flash_set_mode(hal_flash_mode_t mode);

/**
 * @brief 查询 Flash 基本信息
 * @param out 输出信息
 * @return RET_OK 或错误码
 */
ret_code_t hal_flash_get_info(hal_flash_info_t *out);

/**
 * @brief 根据地址查询其所在 Flash region
 * @param addr Flash 地址
 * @param out  输出 region 信息
 * @return RET_OK 或错误码
 */
ret_code_t hal_flash_get_region(uint32_t addr, hal_flash_region_t *out);

/**
 * @brief 取消当前待执行 job
 * @return RET_OK 或错误码
 * @note 当前实现为 best-effort：若硬件操作已经开始，可能无法中途打断
 */
ret_code_t hal_flash_cancel(void);

/**
 * @brief Flash 模块主处理函数
 * @note 裸机/无后台 worker 场景下，需要周期性调用以推进 pending job
 * @note 当前 erase/write 会在这里启动 IT，然后由 Flash IRQ 回调收尾
 */
void hal_flash_main_function(void);
/**============================================================================================ */
/**==================================   异步/非阻塞    ========================================== */
/**============================================================================================ */
/**
 * @brief 异步读取 Flash
 * @param addr Flash 地址
 * @param dst  输出缓冲区
 * @param len  读取长度
 * @return RET_OK 或错误码
 */
ret_code_t hal_flash_read(uint32_t addr, void *dst, uint32_t len);

/**
 * @brief 异步擦除 Flash
 * @param addr 起始地址
 * @param len  覆盖长度
 * @return RET_OK 或错误码
 */
ret_code_t hal_flash_erase(uint32_t addr, uint32_t len);

/**
 * @brief 异步写入 Flash
 * @param addr Flash 地址
 * @param src  输入数据
 * @param len  写入长度
 * @return RET_OK 或错误码
 */
ret_code_t hal_flash_write(uint32_t addr, const void *src, uint32_t len);

/**
 * @brief 异步比较 Flash 内容
 * @param addr Flash 地址
 * @param src  期望数据
 * @param len  比较长度
 * @return RET_OK 或错误码
 * @note 仅表示成功受理作业；比较不一致通过 job_result=FAILED 体现
 */
ret_code_t hal_flash_compare(uint32_t addr, const void *src, uint32_t len);

/**
 * @brief 异步空白检查
 * @param addr Flash 地址
 * @param len  检查长度
 * @param out  结果输出，job 完成后写入
 * @return RET_OK 或错误码
 */
ret_code_t hal_flash_blank_check(uint32_t addr, uint32_t len, bool *out);
/**============================================================================================ */
/**==================================     同步/阻塞   ========================================== */
/**============================================================================================ */
/**
 * @brief 同步读取 Flash
 * @note 同步接口也会占用 HAL 内部 BUSY 状态，不能与异步 job 并发
 */
ret_code_t hal_flash_read_sync(uint32_t addr, void *dst, uint32_t len);

/**
 * @brief 同步擦除 Flash
 * @note 写/擦除语义保持显式，不会隐式先擦后写
 */
ret_code_t hal_flash_erase_sync(uint32_t addr, uint32_t len);

/**
 * @brief 同步写入 Flash
 */
ret_code_t hal_flash_write_sync(uint32_t addr, const void *src, uint32_t len);

/**
 * @brief 同步比较 Flash 内容
 */
ret_code_t hal_flash_compare_sync(uint32_t addr, const void *src, uint32_t len);

/**
 * @brief 检查指定地址范围数据是否是空
 */
ret_code_t hal_flash_blank_check_sync(uint32_t addr, uint32_t len, bool *out);


/**
 * @brief 内部错误弱钩子函数替换内部默认实现
 * @param rc_port port 错误码
 * @param rc_hal  hal 错误码
 * @param api     api名称
 * @param arg0    参数1
 * @param arg1    参数2
 */
void hal_flash_on_port_error(ret_code_t rc_port, ret_code_t rc_hal, const char *api, uint32_t arg0,
                             uint32_t arg1);

#ifdef __cplusplus
}
#endif

#endif  // SMARTLOCK_HAL_FLASH_H
