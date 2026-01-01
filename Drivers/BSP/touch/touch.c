/**
 ****************************************************************************************************
 * @file        touch.c
 * @brief       Touch device glue for GT9xxx driver (provides tp_dev + tp_init stubs)
 ****************************************************************************************************
 */

#include "touch.h"

#include <string.h>

#include "lcd.h"

_m_tp_dev tp_dev;

void tp_adjust(void)
{
    /* Capacitive touch: no calibration */
}

void tp_save_adjust_data(void)
{
    /* Capacitive touch: no calibration */
}

uint8_t tp_get_adjust_data(void)
{
    /* Capacitive touch: no calibration data */
    return 0;
}

void tp_draw_big_point(uint16_t x, uint16_t y, uint16_t color)
{
    (void)x;
    (void)y;
    (void)color;
}

uint8_t tp_init(void)
{
    memset(&tp_dev, 0, sizeof(tp_dev));

    tp_dev.init = gt9xxx_init;
    tp_dev.scan = gt9xxx_scan;
    tp_dev.adjust = tp_adjust;

    /* b7=1: capacitive; b0: lcd dir (0 vertical, 1 horizontal) */
    tp_dev.touchtype = (uint8_t)(0x80u | (lcddev.dir ? 1u : 0u));

    for (uint8_t i = 0; i < CT_MAX_TOUCH; i++)
    {
        tp_dev.x[i] = 0xFFFF;
        tp_dev.y[i] = 0xFFFF;
    }
    tp_dev.sta = 0;

    if (tp_dev.init == NULL)
    {
        return 1;
    }

    return (tp_dev.init() == 0) ? 0 : 1;
}

