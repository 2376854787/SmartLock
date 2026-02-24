#include <stdbool.h>

#include "assert_cus.h"
#include "stm32_hal.h"
#include "blackbox_record.h"
/**
 * @brief 黑盒子的 使能平台实现
 */
void BB_EnableAccess(void) {
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_BKPSRAM_CLK_ENABLE();
    HAL_PWREx_EnableBkUpReg();
    ASSERT_FATAL(__HAL_RCC_BKPSRAM_IS_CLK_ENABLED() != 0u);
}

/**
 * @brief 黑盒子的抽象层实现
 * @return 备份域时钟是否已使能
 */
bool BB_Clock_is_ready(void) {
    return __HAL_RCC_BKPSRAM_IS_CLK_ENABLED();
}



/* ---------- 兼容不同芯片宏：IWDG / WWDG / SOFT / ... ---------- */
#if defined(RCC_CSR_WDGRSTF)
#define BB_FLAG_IWDG_CSR (RCC_CSR_WDGRSTF)
#elif defined(RCC_CSR_IWDGRSTF)
#define BB_FLAG_IWDG_CSR (RCC_CSR_IWDGRSTF)
#else
#define BB_FLAG_IWDG_CSR (0u)
#endif

/* 窗口看门狗 */
#if defined(RCC_CSR_WWDGRSTF)
#define BB_FLAG_WWDG_CSR (RCC_CSR_WWDGRSTF)
#else
#define BB_FLAG_WWDG_CSR (0u)
#endif

/* 软件复位 */
#if defined(RCC_CSR_SFTRSTF)
#define BB_FLAG_SOFT_CSR (RCC_CSR_SFTRSTF)
#else
#define BB_FLAG_SOFT_CSR (0u)
#endif

/* 低功耗唤醒复位 */
#if defined(RCC_CSR_LPWRRSTF)
#define BB_FLAG_LPWR_CSR (RCC_CSR_LPWRRSTF)
#else
#define BB_FLAG_LPWR_CSR (0u)
#endif

/* 复位引脚复位 */
#if defined(RCC_CSR_PINRSTF)
#define BB_FLAG_PIN_CSR (RCC_CSR_PINRSTF)
#else
#define BB_FLAG_PIN_CSR (0u)
#endif

/* 欠压复位 */
#if defined(RCC_CSR_BORRSTF)
#define BB_FLAG_BOR_CSR (RCC_CSR_BORRSTF)
#else
#define BB_FLAG_BOR_CSR (0u)
#endif

/* 上电复位 */
#if defined(RCC_CSR_PORRSTF)
#define BB_FLAG_POR_CSR (RCC_CSR_PORRSTF)
#else
#define BB_FLAG_POR_CSR (0u)
#endif

/* G0 等系列可能还有 OBLRSTF（选项字节加载复位），可加映射 */
#if defined(RCC_CSR_OBLRSTF)
#define BB_FLAG_OBL_CSR (RCC_CSR_OBLRSTF)
#else
#define BB_FLAG_OBL_CSR (0u)
#endif

/* ---------- H7: RCC->RSR flags（字段示例：IWDG1RSTF / WWDG1RSTF / SFTRSTF ...） ---------- */
#if defined(RCC_RSR_IWDG1RSTF)
#define BB_FLAG_IWDG_RSR (RCC_RSR_IWDG1RSTF)
#elif defined(RCC_RSR_IWDGRSTF)
#define BB_FLAG_IWDG_RSR (RCC_RSR_IWDGRSTF)
#else
#define BB_FLAG_IWDG_RSR (0u)
#endif

#if defined(RCC_RSR_WWDG1RSTF)
#define BB_FLAG_WWDG_RSR (RCC_RSR_WWDG1RSTF)
#elif defined(RCC_RSR_WWDGRSTF)
#define BB_FLAG_WWDG_RSR (RCC_RSR_WWDGRSTF)
#else
#define BB_FLAG_WWDG_RSR (0u)
#endif

#if defined(RCC_RSR_SFTRSTF)
#define BB_FLAG_SOFT_RSR (RCC_RSR_SFTRSTF)
#else
#define BB_FLAG_SOFT_RSR (0u)
#endif

#if defined(RCC_RSR_LPWRRSTF)
#define BB_FLAG_LPWR_RSR (RCC_RSR_LPWRRSTF)
#else
#define BB_FLAG_LPWR_RSR (0u)
#endif

#if defined(RCC_RSR_PINRSTF)
#define BB_FLAG_PIN_RSR (RCC_RSR_PINRSTF)
#else
#define BB_FLAG_PIN_RSR (0u)
#endif

#if defined(RCC_RSR_BORRSTF)
#define BB_FLAG_BOR_RSR (RCC_RSR_BORRSTF)
#else
#define BB_FLAG_BOR_RSR (0u)
#endif

#if defined(RCC_RSR_PORRSTF)
#define BB_FLAG_POR_RSR (RCC_RSR_PORRSTF)
#else
#define BB_FLAG_POR_RSR (0u)
#endif
/* ===================== 读原因 + 清 flags ===================== */
bb_reset_reason_t BB_Port_ReadResetReasonAndClearFlags(void) {
    bb_reset_reason_t reason = BB_RESET_UNKNOW;

    /* 1) 读 flags */
#if defined(RCC_RSR_RMVF)
    /* H7：Reset Status Register (RSR) */
    const uint32_t f = RCC->RSR;

    /* 2) 选一个"主因"（flags 可能多位同时置位，按优先级归因） */
    if ((BB_FLAG_WWDG_RSR != 0u) && (f & BB_FLAG_WWDG_RSR))
        reason = BB_RESET_WWDG;
    else if ((BB_FLAG_IWDG_RSR != 0u) && (f & BB_FLAG_IWDG_RSR))
        reason = BB_RESET_IWDG;
    else if ((BB_FLAG_SOFT_RSR != 0u) && (f & BB_FLAG_SOFT_RSR))
        reason = BB_RESET_SOFT;
    else if ((BB_FLAG_LPWR_RSR != 0u) && (f & BB_FLAG_LPWR_RSR))
        reason = BB_RESET_LPWR;
    else if ((BB_FLAG_PIN_RSR != 0u) && (f & BB_FLAG_PIN_RSR))
        reason = BB_RESET_PIN;
    else if ((BB_FLAG_BOR_RSR != 0u) && (f & BB_FLAG_BOR_RSR))
        reason = BB_RESET_BOR;
    else if ((BB_FLAG_POR_RSR != 0u) && (f & BB_FLAG_POR_RSR))
        reason = BB_RESET_POR;

    /* 3) 清 flags：写 RMVF（H7 的 RSR 也通过 RMVF 清除）*/
    RCC->RSR |= RCC_RSR_RMVF;

#else
    /* 非 H7：CSR */
    const uint32_t f = RCC->CSR;

    if ((BB_FLAG_WWDG_CSR != 0u) && (f & BB_FLAG_WWDG_CSR)) {
        reason = BB_RESET_WWDG;
    } else if ((BB_FLAG_IWDG_CSR != 0u) && (f & BB_FLAG_IWDG_CSR)) {
        reason = BB_RESET_IWDG;
    } else if ((BB_FLAG_SOFT_CSR != 0u) && (f & BB_FLAG_SOFT_CSR)) {
        reason = BB_RESET_SOFT;
    } else if ((BB_FLAG_LPWR_CSR != 0u) && (f & BB_FLAG_LPWR_CSR)) {
        reason = BB_RESET_LPWR;
    } else if ((BB_FLAG_PIN_CSR != 0u) && (f & BB_FLAG_PIN_CSR)) {
        reason = BB_RESET_PIN;
    } else if ((BB_FLAG_BOR_CSR != 0u) && (f & BB_FLAG_BOR_CSR)) {
        reason = BB_RESET_BOR;
    } else if ((BB_FLAG_POR_CSR != 0u) && (f & BB_FLAG_POR_CSR)) {
        reason = BB_RESET_POR;
    } else if ((BB_FLAG_OBL_CSR != 0u) && (f & BB_FLAG_OBL_CSR)) {
        reason = BB_RESET_SOFT; /* 也可单独加 OBL 枚举 */
    }
    /* 清 flags：推荐 |=，避免影响保留位 */
    RCC->CSR |= RCC_CSR_RMVF;
#endif

    return reason;
}
