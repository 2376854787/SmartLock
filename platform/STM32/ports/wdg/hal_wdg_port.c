#include "APP_config.h"

#if (defined(CFG_FEAT_HAL_WDG) && (CFG_FEAT_HAL_WDG == 1)) && \
    (defined(CFG_TARGET_PLATFORM_STM32_HAL) && (CFG_TARGET_PLATFORM_STM32_HAL == 1))

#include <stdbool.h>
#include <stdint.h>

#include "hal_wdg_port.h"
#include "stm32_hal.h"

static IWDG_HandleTypeDef s_hiwdg;
static volatile bool s_hw_inited = false;

/**
 * @brief 假设使用 LSI 32k hz 时钟进行256 分频将计算出所需的值输出
 * @param timeout_ms 需要设置的独立看门狗超时时间 最大32s
 * @param out_presc  输出设置的分频系数
 * @param out_reload 输出计算出的重载值
 * @return 32位状态码
 * @note 内部辅助函数
 */
static ret_code_t iwdg_calc(uint32_t timeout_ms, uint32_t *out_presc, uint32_t *out_reload) {
    if (!out_presc || !out_reload) {
        return RET_MAKE_PARAM(RET_MOD_PORT, RET_SUB_PORT_STM32, RET_R_NULL_PTR);
    }
    const uint32_t lsi_hz  = 32000u;
    const uint32_t presc   = IWDG_PRESCALER_256; /* tick ≈ 125Hz */
    const uint32_t tick_hz = lsi_hz / 256u;

    uint32_t reload        = (timeout_ms * tick_hz) / 1000u;
    if (reload == 0u) reload = 1u;
    if (reload > 0x0FFFu) reload = 0x0FFFu;

    *out_presc  = presc;
    *out_reload = reload;
    return RET_OK;
}

/**
 * @brief 用于调试器调试冻结 Debug寄存器 防止看门狗复位
 * @param enable 是否冻结 Debug寄存器
 * @note 内部辅助函数
 */
static void wdg_debug_freeze_apply(bool enable) {
#if defined(DBGMCU_APB1_FZ_DBG_IWDG_STOP)
#ifdef __HAL_RCC_DBGMCU_CLK_ENABLE
    __HAL_RCC_DBGMCU_CLK_ENABLE();
#endif
    if (enable) {
        /* 置 1 */
        DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP;
    } else {
        /* 置 0*/
        DBGMCU->APB1FZ &= ~DBGMCU_APB1_FZ_DBG_IWDG_STOP;
    }
#else
    (void)enable;
#endif
}
/**
 * @brief 配置硬件看门狗
 * @param cfg 看门狗配置
 * @return 32位状态码
 */
ret_code_t hal_wdg_port_init(const hal_wdg_cfg_t *cfg) {
    /* 参数检查 */
    if (cfg == NULL) return RET_MAKE_PARAM(RET_MOD_PORT, RET_SUB_PORT_STM32, RET_R_NULL_PTR);
    if (cfg->mode != HAL_WDG_MODE_IWDG) {
        return RET_MAKE_PARAM(RET_MOD_PORT, RET_SUB_PORT_STM32, RET_R_UNSUPPORTED);
    }
    /* 计算出所需要的 预分频系数 重载值 */
    uint32_t presc = 0, reload = 0;
    const ret_code_t rc = iwdg_calc(cfg->timeout_ms, &presc, &reload);
    if (ret_is_err(rc)) return rc;
    /* 根据配置查看是否冻结 */
    wdg_debug_freeze_apply(cfg->debug_freeze);
    s_hiwdg.Instance       = IWDG;
    s_hiwdg.Init.Prescaler = presc;
    s_hiwdg.Init.Reload    = reload;
    if (HAL_IWDG_Init(&s_hiwdg) != HAL_OK) {
        return RET_MAKE_IO(RET_MOD_PORT, RET_SUB_PORT_STM32, RET_R_HW_FAULT);
    }
    s_hw_inited = true;
    return RET_OK;
}
/**
 * @brief
 * @return 32位状态码
 */
ret_code_t hal_wdg_port_kick(void) {
    /* 检查是否初始化了看门狗 */
    if (!s_hw_inited) return RET_MAKE_STATE(RET_MOD_PORT, RET_SUB_PORT_STM32, RET_R_NOT_READY);
    /* 喂狗 */
    if (HAL_IWDG_Refresh(&s_hiwdg) != HAL_OK) {
        return RET_MAKE_IO(RET_MOD_PORT, RET_SUB_PORT_STM32, RET_R_HW_FAULT);
    }
    return RET_OK;
}

#endif
