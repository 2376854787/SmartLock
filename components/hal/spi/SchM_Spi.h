#ifndef SCHM_SPI_H
#define SCHM_SPI_H

#include <stdbool.h>
#include <stdint.h>

#include "ret_code_t.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uintptr_t SchM_Spi_CritStateType;
typedef uintptr_t SchM_Spi_LockHandleType;

/**
 * @brief 查询内核是否已启动（用于决定是否创建互斥锁）
 */
bool SchM_Spi_KernelIsRunning(void);

/**
 * @brief 创建 SPI 互斥锁（由 OS 适配层实现）
 * @param out           输出锁句柄
 * @param name          锁名称
 * @param recursive     是否递归锁
 * @param prio_inherit  是否优先级继承
 * @return RET_OK 或错误码
 */
ret_code_t SchM_Spi_LockCreate(SchM_Spi_LockHandleType *out, const char *name, bool recursive,
                               bool prio_inherit);

/**
 * @brief 删除 SPI 互斥锁
 */
void SchM_Spi_LockDelete(SchM_Spi_LockHandleType lock);

/**
 * @brief 获取 SPI 互斥锁
 */
ret_code_t SchM_Spi_Lock(SchM_Spi_LockHandleType lock, uint32_t timeout_ms);

/**
 * @brief 释放 SPI 互斥锁
 */
void SchM_Spi_Unlock(SchM_Spi_LockHandleType lock);

/**
 * @brief 进入 SPI 独占区（关中断临界区）
 */
void SchM_Enter_Spi_ExclusiveArea(SchM_Spi_CritStateType *state);

/**
 * @brief 退出 SPI 独占区（恢复中断临界区）
 */
void SchM_Exit_Spi_ExclusiveArea(SchM_Spi_CritStateType state);

#ifdef __cplusplus
}
#endif

#endif

