#ifndef HAL_SPI_INTERNAL_H
#define HAL_SPI_INTERNAL_H

#include "hal_spi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*hal_spi_sync_observer_t)(void *user, const hal_spi_event_t *evt);

/**
 * @brief 注册/注销 SPI 同步观察者（内部接口）
 * @param dev  设备句柄
 * @param cb   观察者回调，NULL 表示注销
 * @param user 观察者上下文
 * @return RET_OK 或错误码
 * @note 同步封装模块通过该观察者等待 DONE/ERROR，避免把同步等待状态和 OSAL 依赖回灌到 hal_spi.c。
 */
ret_code_t hal_spi_dev_set_sync_observer(hal_spi_dev_t *dev, hal_spi_sync_observer_t cb,
                                         void *user);

#ifdef __cplusplus
}
#endif

#endif
