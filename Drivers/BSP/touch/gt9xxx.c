/**
 ****************************************************************************************************
 * @file        gt9xxx.c
 * @brief       GT9xxx (GT911/GT9147/GT1151/GT9271...) CTP driver (software I2C) - RTOS-safe
 *
 * RTOS migration:
 *   1) One I2C transaction (start -> stop) is guarded by mutex (touch_port_lock/unlock).
 *   2) All millisecond delays yield CPU when scheduler is running (touch_port_delay_ms).
 *   3) Robust I2C address handling: GT9xxx can respond at 0x14 or 0x5D (7-bit).
 *      This driver forces both variants via INT level during reset, and probes to select.
 ****************************************************************************************************
 */

#include "string.h"

#include "APP_config.h"

#include "lcd.h"
#include "touch.h"
#include "ctiic.h"
#include "gt9xxx.h"

#include "log.h"
#include "touch_port.h"


/* Note: except GT9271 supports 10 touches, others typically support 5. */
uint8_t g_gt_tnum = 5;

/* Current I2C 8-bit command (address + R/W). Default is 0x14 (7-bit) => 0x28/0x29 */
static uint8_t s_gt_cmd_wr = GT9XXX_CMD_WR;
static uint8_t s_gt_cmd_rd = GT9XXX_CMD_RD;

static void gt9xxx_int_as_output(uint8_t level) {
    GPIO_InitTypeDef gpio_init_struct;

    gpio_init_struct.Pin = GT9XXX_INT_GPIO_PIN;
    gpio_init_struct.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init_struct.Pull = GPIO_PULLUP;
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GT9XXX_INT_GPIO_PORT, &gpio_init_struct);

    HAL_GPIO_WritePin(GT9XXX_INT_GPIO_PORT, GT9XXX_INT_GPIO_PIN, level ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void gt9xxx_int_as_input_nopull(void) {
    GPIO_InitTypeDef gpio_init_struct;

    gpio_init_struct.Pin = GT9XXX_INT_GPIO_PIN;
    gpio_init_struct.Mode = GPIO_MODE_INPUT;
    gpio_init_struct.Pull = GPIO_NOPULL;
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GT9XXX_INT_GPIO_PORT, &gpio_init_struct);
}

/*
 * Goodix CTP I2C address select (common behavior):
 *   - Keep INT low during reset release => address 0x14 (7-bit) => 0x28/0x29
 *   - Keep INT high during reset release => address 0x5D (7-bit) => 0xBA/0xBB
 *
 * If your module has fixed address (wiring/pull-up differs), the probe logic below will still
 * find the responding one.
 */
static void gt9xxx_hw_reset_with_int_level(uint8_t int_high) {
    /* Drive INT level before reset release (address latch window) */
    gt9xxx_int_as_output(int_high ? 1 : 0);

    GT9XXX_RST(0);
    touch_port_delay_ms(10);
    GT9XXX_RST(1);
    touch_port_delay_ms(10);

    /* Switch INT back to input (floating) for normal operation */
    gt9xxx_int_as_input_nopull();

    touch_port_delay_ms(100);
}

static uint8_t gt9xxx_is_valid_pid(const uint8_t pid4[4]) {
    /* Valid IDs in the original sample: "911", "9147", "1158", "9271" */
    if (pid4[0] == '9' && pid4[1] == '1' && pid4[2] == '1') return 1;
    if (pid4[0] == '9' && pid4[1] == '1' && pid4[2] == '4' && pid4[3] == '7') return 1;
    if (pid4[0] == '1' && pid4[1] == '1' && pid4[2] == '5' && pid4[3] == '8') return 1;
    if (pid4[0] == '9' && pid4[1] == '2' && pid4[2] == '7' && pid4[3] == '1') return 1;
    return 0;
}

static uint8_t gt9xxx_probe_addr(uint8_t cmd_wr, uint8_t cmd_rd, uint8_t pid4_out[4]);

/**
 * @brief       Write GT9xxx register
 * @param       reg: register addr
 * @param       buf: data
 * @param       len: data length
 * @retval      0 ok; 1 fail
 */
uint8_t gt9xxx_wr_reg(uint16_t reg, uint8_t *buf, uint8_t len) {
    uint8_t i;
    uint8_t ret = 0;

    touch_port_lock();

    ct_iic_start();
    ct_iic_send_byte(s_gt_cmd_wr);
    if (ct_iic_wait_ack()) {
        ret = 1;
        goto out;
    }

    ct_iic_send_byte((uint8_t) (reg >> 8));
    ct_iic_wait_ack();

    ct_iic_send_byte((uint8_t) (reg & 0xFF));
    ct_iic_wait_ack();

    for (i = 0; i < len; i++) {
        ct_iic_send_byte(buf[i]);
        if (ct_iic_wait_ack()) {
            ret = 1;
            goto out;
        }
    }

out:
    ct_iic_stop();
    touch_port_unlock();
    return ret;
}

/**
 * @brief       Read GT9xxx register
 * @param       reg: register addr
 * @param       buf: out data
 * @param       len: length
 * @retval      0 ok; 1 fail
 */
uint8_t gt9xxx_rd_reg(uint16_t reg, uint8_t *buf, uint8_t len) {
    uint8_t i;
    uint8_t ret = 0;

    touch_port_lock();

    ct_iic_start();
    ct_iic_send_byte(s_gt_cmd_wr);
    if (ct_iic_wait_ack()) {
        ret = 1;
        goto out;
    }

    ct_iic_send_byte((uint8_t) (reg >> 8));
    ct_iic_wait_ack();

    ct_iic_send_byte((uint8_t) (reg & 0xFF));
    ct_iic_wait_ack();

    ct_iic_start();
    ct_iic_send_byte(s_gt_cmd_rd);
    if (ct_iic_wait_ack()) {
        ret = 1;
        goto out;
    }

    for (i = 0; i < len; i++) {
        buf[i] = ct_iic_read_byte(i == (len - 1) ? 0 : 1);
    }

out:
    ct_iic_stop();
    touch_port_unlock();
    return ret;
}

static uint8_t gt9xxx_probe_addr(uint8_t cmd_wr, uint8_t cmd_rd, uint8_t pid4_out[4]) {
    uint8_t pid4[4];

    s_gt_cmd_wr = cmd_wr;
    s_gt_cmd_rd = cmd_rd;

    if (gt9xxx_rd_reg(GT9XXX_PID_REG, pid4, 4) == 0) {
        if (gt9xxx_is_valid_pid(pid4)) {
            if (pid4_out) {
                pid4_out[0] = pid4[0];
                pid4_out[1] = pid4[1];
                pid4_out[2] = pid4[2];
                pid4_out[3] = pid4[3];
            }
            return 1;
        }
    }

    return 0;
}

/**
 * @brief       Init GT9xxx
 * @retval      0 ok; 1 fail
 */
uint8_t gt9xxx_init(void) {
    GPIO_InitTypeDef gpio_init_struct;
    uint8_t pid4[4];

    GT9XXX_RST_GPIO_CLK_ENABLE();
    GT9XXX_INT_GPIO_CLK_ENABLE();

    /* RST pin */
    gpio_init_struct.Pin = GT9XXX_RST_GPIO_PIN;
    gpio_init_struct.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init_struct.Pull = GPIO_PULLUP;
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GT9XXX_RST_GPIO_PORT, &gpio_init_struct);

    /* INT pin: default input pull-up (will be reconfigured during reset) */
    gpio_init_struct.Pin = GT9XXX_INT_GPIO_PIN;
    gpio_init_struct.Mode = GPIO_MODE_INPUT;
    gpio_init_struct.Pull = GPIO_PULLUP;
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GT9XXX_INT_GPIO_PORT, &gpio_init_struct);

    /* RTOS port init (mutex) */
    touch_port_init();

    /* software I2C init */
    ct_iic_init();

    /* Try address 0x14 (0x28/0x29) by INT low */
    gt9xxx_hw_reset_with_int_level(0);
    if (!gt9xxx_probe_addr(0x28, 0x29, pid4)) {
        /* Try address 0x5D (0xBA/0xBB) by INT high */
        gt9xxx_hw_reset_with_int_level(1);
        if (!gt9xxx_probe_addr(0xBA, 0xBB, pid4)) {
            /* Neither address responds */
            return 1;
        }
    }

    /* Print ID */
    {
        char s[5];
        s[0] = (char) pid4[0];
        s[1] = (char) pid4[1];
        s[2] = (char) pid4[2];
        s[3] = (char) pid4[3];
        s[4] = 0;
        LOG_I("gt9xx", "CTP ID:%s\r\n", s);
        LOG_I("gt9xx", "CTP I2C 7-bit addr:0x%02X\r\n", (unsigned)(s_gt_cmd_wr >> 1));

        if (strcmp(s, "9271") == 0) {
            g_gt_tnum = 10;
        } else {
            g_gt_tnum = 5;
        }
    }

    /* Soft reset */
    {
        uint8_t t;
        t = 0x02;
        (void) gt9xxx_wr_reg(GT9XXX_CTRL_REG, &t, 1);
        touch_port_delay_ms(10);
        t = 0x00;
        (void) gt9xxx_wr_reg(GT9XXX_CTRL_REG, &t, 1);
    }

    return 0;
}

/* GT9xxx touch point register list (max 10) */
const uint16_t GT9XXX_TPX_TBL[10] = {
    GT9XXX_TP1_REG, GT9XXX_TP2_REG, GT9XXX_TP3_REG, GT9XXX_TP4_REG, GT9XXX_TP5_REG,
    GT9XXX_TP6_REG, GT9XXX_TP7_REG, GT9XXX_TP8_REG, GT9XXX_TP9_REG, GT9XXX_TP10_REG,
};

static void gt9xxx_map_point(uint16_t raw_x, uint16_t raw_y, uint16_t *x_out, uint16_t *y_out)
{
    uint16_t x = raw_x;
    uint16_t y = raw_y;

    /* Align touch coordinates with LCD orientation.
     * tp_dev.touchtype:
     *  - b0: 0 portrait (X=L/R, Y=U/D); 1 landscape (X=U/D, Y=L/R)
     *  - b7: 1 capacitive
     *
     * If you see “touch works but hits the wrong widget” in landscape, it is usually because
     * the logical screen orientation is swapped/mirrored relative to the touch controller.
     * Use the invert macros below to fix mirror issues without rewriting the mapping logic:
     * - TOUCH_LANDSCAPE_INVERT_X / TOUCH_LANDSCAPE_INVERT_Y
     * - TOUCH_PORTRAIT_INVERT_X   / TOUCH_PORTRAIT_INVERT_Y
     */
    if (tp_dev.touchtype & 0x01u) {
        /* landscape: swap axes */
        uint16_t t = x;
        x = y;
        y = t;

#if defined(TOUCH_LANDSCAPE_INVERT_X) && (TOUCH_LANDSCAPE_INVERT_X)
        if (lcddev.width > 0) x = (uint16_t)(lcddev.width - 1u - x);
#endif
#if defined(TOUCH_LANDSCAPE_INVERT_Y) && (TOUCH_LANDSCAPE_INVERT_Y)
        if (lcddev.height > 0) y = (uint16_t)(lcddev.height - 1u - y);
#endif
    } else {
#if defined(TOUCH_PORTRAIT_INVERT_X) && (TOUCH_PORTRAIT_INVERT_X)
        if (lcddev.width > 0) x = (uint16_t)(lcddev.width - 1u - x);
#endif
#if defined(TOUCH_PORTRAIT_INVERT_Y) && (TOUCH_PORTRAIT_INVERT_Y)
        if (lcddev.height > 0) y = (uint16_t)(lcddev.height - 1u - y);
#endif
    }

    if (lcddev.width > 0 && x >= lcddev.width) x = (uint16_t)(lcddev.width - 1u);
    if (lcddev.height > 0 && y >= lcddev.height) y = (uint16_t)(lcddev.height - 1u);

    if (x_out) *x_out = x;
    if (y_out) *y_out = y;
}

/**
 * @brief       Scan touch points (polling)
 * @param       mode: unused for GT9xxx (kept for compatibility)
 * @retval      0 ok; 1 fail
 */
uint8_t gt9xxx_scan(uint8_t mode) {
    uint8_t buf[4];
    uint8_t i;
    uint8_t res;
    uint16_t tempsta;
    uint8_t gst = 0;
    uint8_t touch_num = 0;
    uint32_t now_ms;
    static uint32_t last_poll_ms = 0;

    (void)mode;

    now_ms = touch_port_get_tick_ms();
    if ((uint32_t)(now_ms - last_poll_ms) < (uint32_t)TOUCH_POLL_INTERVAL_MS) {
        return 0;
    }
    last_poll_ms = now_ms;

    res = gt9xxx_rd_reg(GT9XXX_GSTID_REG, &gst, 1);
    if (res != 0) {
        return res;
    }

    touch_num = (uint8_t)(gst & 0x0F);

    if ((gst & 0x80u) && (touch_num > 0) && (touch_num <= g_gt_tnum)) {
        tempsta = 0;
        for (i = 0; i < touch_num; i++) {
            uint16_t raw_x;
            uint16_t raw_y;
            uint16_t x;
            uint16_t y;

            res = gt9xxx_rd_reg(GT9XXX_TPX_TBL[i], buf, 4);
            if (res != 0) {
                continue;
            }

            raw_x = (uint16_t)(((uint16_t)buf[1] << 8) | buf[0]);
            raw_y = (uint16_t)(((uint16_t)buf[3] << 8) | buf[2]);

            gt9xxx_map_point(raw_x, raw_y, &x, &y);
            tp_dev.x[i] = x;
            tp_dev.y[i] = y;
            tempsta |= (uint16_t)(1u << i);
        }

        tp_dev.sta = (uint16_t)(tempsta | TP_PRES_DOWN | TP_CATH_PRES);
    } else if (gst & 0x80u) {
        /* Data ready but no valid touch point => released */
        if (tp_dev.sta & TP_PRES_DOWN) {
            tp_dev.sta &= (uint16_t)~TP_PRES_DOWN;
        } else {
            tp_dev.x[0] = 0xFFFF;
            tp_dev.y[0] = 0xFFFF;
            tp_dev.sta &= 0xE000;
        }
    } else {
        /* No new data: keep last state to avoid missing very short presses */
    }

    /* Clear status only when controller reports data-ready */
    if (gst & 0x80u) {
        uint8_t clr = 0;
        (void)gt9xxx_wr_reg(GT9XXX_GSTID_REG, &clr, 1);
    }

    return 0;
}
