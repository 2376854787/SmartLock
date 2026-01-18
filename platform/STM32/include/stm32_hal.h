#ifndef STM32_HAL_H
#define STM32_HAL_H

#include "stm32_hal_config.h"
/* hal抽象选择宏 */
#if defined(USE_STM32_HAL)
/* 保证有且只定义了一个平台的宏 */
#if (defined(STM32C0XX) + defined(STM32F0XX) + defined(STM32F1XX) + defined(STM32F2XX) +     \
         defined(STM32F3XX) + defined(STM32F4XX) + defined(STM32F7XX) + defined(STM32H5XX) + \
         defined(STM32H7XX) + defined(STM32G0XX) + defined(STM32G4XX) + defined(STM32L0XX) + \
         defined(STM32L1XX) + defined(STM32L4XX) + defined(STM32L5XX) + defined(STM32N6XX) + \
         defined(STM32U0XX) + defined(STM32U3XX) + defined(STM32U5XX) !=                     \
     1)
#error "Select exactly one STM32 series macro"
#endif

/* stm32系列宏开关 */
#ifdef STM32C0XX
#include "stm32c0xx_hal.h"
#endif

#ifdef STM32F0XX
#include "stm32f0xx_hal.h"
#endif

#ifdef STM32F1XX
#include "stm32f1xx_hal.h"
#endif

#ifdef STM32F2XX
#include "stm32f2xx_hal.h"
#endif

#ifdef STM32F3XX
#include "stm32f3xx_hal.h"
#endif

#ifdef STM32F4XX
#include "stm32f4xx_hal.h"
#endif

#ifdef STM32F7XX
#include "stm32f7xx_hal.h"
#endif

#ifdef STM32H5XX
#include "stm32h5xx_hal.h"
#endif

#ifdef STM32H7XX
#include "stm32h7xx_hal.h"
#endif

#ifdef STM32G0XX
#include "stm32g0xx_hal.h"
#endif

#ifdef STM32G4XX
#include "stm32g4xx_hal.h"
#endif

#ifdef STM32L0XX
#include "stm32l0xx_hal.h"
#endif

#ifdef STM32L1XX
#include "stm32l1xx_hal.h"
#endif

#ifdef STM32L4XX
#include "stm32l4xx_hal.h"
#endif

#ifdef STM32L5XX
#include "stm32l5xx_hal.h"
#endif

#ifdef STM32N6XX
#include "stm32n6xx_hal.h"
#endif

#ifdef STM32U0XX
#include "stm32u0xx_hal.h"
#endif

#ifdef STM32U3XX
#include "stm32u3xx_hal.h"
#endif

#ifdef STM32U5XX
#include "stm32u5xx_hal.h"
#endif

#endif
#endif
