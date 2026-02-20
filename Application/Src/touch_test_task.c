/**
 * @file    touch_test_task.c
 * @brief   GT911 触摸屏测试任务 — 中断驱动 + 信号量通知 (使用 HAL GPIO 中断 API)
 *
 * 工作模式:
 *   GT911 触摸事件 → INT 引脚下降沿 → HAL GPIO Port 中断分发 →
 *   回调释放信号量 → 任务唤醒读取触摸数据
 */

#include "touch_test_task.h"

#include <stddef.h>

#include "board_gpio_ids.h"
#include "gt911.h"
#include "hal_gpio.h"
#include "log.h"
#include "osal.h"

#define TAG "TOUCH"

/* GT911 设备实例 */
static gt911_dev_t s_touch;

/* 触摸中断通知信号量 */
static osal_sem_t s_touch_sem;

/* ======================== EXTI 中断回调 ======================== */

/**
 * @brief  HAL GPIO 中断回调
 * @param  user_data  用户数据 (信号量句柄)
 */
static void touch_irq_handler_cb(void* user_data) {
    const osal_sem_t sem = (osal_sem_t)user_data;
    if (sem != NULL) {
        (void)OSAL_sem_give_from_isr(sem);
    }
}

/* ======================== RTOS 任务入口 ======================== */

void StartTouchTestTask(void* argument) {
    (void)argument;

    /* 等系统稳定 */
    (void)OSAL_delay_ms(500);

    /* 创建二值信号量（初始值 0，触摸中断释放） */
    if (OSAL_sem_create(&s_touch_sem, "touch_sem", 0, 1) != RET_OK)
    {
        LOG_E(TAG, "信号量创建失败");

        return;
    }

    /* ---- 初始化 GT911 ---- */
    const gt911_cfg_t cfg = {.gpio_id_scl  = HAL_GPIO_ID_CT_SCL,
                             .gpio_id_sda  = HAL_GPIO_ID_CT_SDA,
                             .gpio_id_rst  = HAL_GPIO_ID_CT_RST,
                             .gpio_id_int  = HAL_GPIO_ID_CT_INT,
                             .i2c_addr     = GT911_ADDR_LOW,
                             .max_x        = 480,
                             .max_y        = 800,
                             .refresh_rate = 100};

    ret_code_t rc         = gt911_init(&s_touch, &cfg);
    if (rc != RET_OK) {
        LOG_E(TAG, "GT911 初始化失败! rc=0x%08lX", (unsigned long)rc);

        return;
    }

    LOG_I(TAG, "GT911 初始化成功, addr=0x%02X", cfg.i2c_addr);

    /* ---- 配置中断 ---- */

    /* 1. 将 INT 引脚重新配置为下降沿触发 */
    const hal_gpio_cfg_t int_irq_cfg = {
        .dir           = HAL_GPIO_DIR_IN,
        .out_type      = HAL_GPIO_OUT_PP,
        .pull          = HAL_GPIO_PULL_NONE,
        .speed         = HAL_GPIO_SPEED_LOW,
        .irq           = HAL_GPIO_IRQ_FALLING, /* 下降沿触发 */
        .alternate     = HAL_GPIO_AF_NONE,
        .default_level = HAL_GPIO_LEVEL_LOW,
    };
    rc = hal_gpio_config(s_touch.intr, &int_irq_cfg);
    if (rc != RET_OK) {
        LOG_E(TAG, "GPIO 中断配置失败");
    }

    /* 2. 注册中断回调 */
    rc = hal_gpio_register_irq(s_touch.intr, touch_irq_handler_cb, s_touch_sem);
    if (rc != RET_OK) {
        LOG_E(TAG, "注册中断回调失败! rc=0x%08lX", (unsigned long)rc);
    } else {
        LOG_I(TAG, "HAL GPIO 中断回调已注册");
    }

    /* ---- 主循环: 等待中断通知再读取 ---- */
    gt911_touch_data_t data;

    for (;;) {
        /* 阻塞等待触摸中断信号量，最长 500ms 超时（兜底防卡死） */
        const ret_code_t sem_rc = OSAL_sem_take(s_touch_sem, 500);

        if (sem_rc == RET_OK || ret_is_timeout(sem_rc)) {
            /* 中断触发 or 超时 (超时也读一下，防止中断丢失或未触发)
               其实超时读一下也没坏处，当作低频轮询兜底
            */
            rc = gt911_read_touch(&s_touch, &data);
            if (rc == RET_OK && data.count > 0) {
                for (uint8_t i = 0; i < data.count; ++i) {
                    LOG_I(TAG, "P%u  x=%u  y=%u  size=%u", data.points[i].id, data.points[i].x,
                          data.points[i].y, data.points[i].size);
                }
            }
        }
    }
}
