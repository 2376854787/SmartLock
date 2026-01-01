#include "lvgl_port.h"
#include "lvgl.h"
#include "lcd.h"
#include "gt9147_compat.h"
#include "log.h"

static lv_disp_draw_buf_t draw_buf;
/* Display draw buffer height (in lines).
 * RAM usage ~= LV_HOR_RES_MAX * LVGL_BUF_LINES * sizeof(lv_color_t).
 * For 800x480 RGB565: 800 * 4 * 2 = 6.4KB.
 * Increase if you want faster refresh and have RAM; decrease to save RAM.
 */
#define LVGL_BUF_LINES 4
static lv_color_t buf1[LV_HOR_RES_MAX * LVGL_BUF_LINES];
static lv_coord_t last_x = 0;
static lv_coord_t last_y = 0;
static bool touch_ready = false;
static bool last_pressed = false;

/* LVGL display flush callback:
 * - LVGL renders into `buf1` (line buffer).
 * - We push the requested area directly to the LCD GRAM (RGB565).
 * - Must call `lv_disp_flush_ready()` when done.
 */
static void disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint16_t x, y;
    uint32_t width = area->x2 - area->x1 + 1;
    uint32_t height = area->y2 - area->y1 + 1;

    lcd_set_window(area->x1, area->y1, width, height);
    lcd_write_ram_prepare();

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            LCD->LCD_RAM = color_p->full;
            color_p++;
        }
    }

    lv_disp_flush_ready(disp);
}

/* LVGL input device read callback (pointer):
 * - Reads touch status/coordinates from the GT9xxx compat layer.
 * - Coordinate mapping (landscape swap / mirror fix) is handled in the GT9xxx driver:
 *   see `Drivers/BSP/touch/gt9xxx.c` (`gt9xxx_map_point()` and invert macros).
 * - When released, LVGL expects the last known point coordinates.
 */
static void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
    (void) indev_drv;
    uint16_t x = 0;
    uint16_t y = 0;
    bool pressed = false;

    if (touch_ready) {
        if (!gt9147_read_point(&x, &y, &pressed)) {
            pressed = false;
        }
    }

    if (pressed) {
        last_x = (lv_coord_t)x;
        last_y = (lv_coord_t)y;
        if (!last_pressed) {
            LOG_D("TOUCH", "pressed: x=%u y=%u (disp %ux%u dir=%u)",
                  (unsigned)x, (unsigned)y, (unsigned)lcddev.width, (unsigned)lcddev.height, (unsigned)lcddev.dir);
        }
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = last_x;
        data->point.y = last_y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
        data->point.x = last_x;
        data->point.y = last_y;
    }

    last_pressed = pressed;
}

void lv_port_disp_init(void) {
    static lv_disp_drv_t disp_drv;
    uint32_t buf_size = lcddev.width * LVGL_BUF_LINES;

    /* Note: `LVGL_BUF_LINES` controls RAM usage and flush chunk size. */
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, buf_size);

    lv_disp_drv_init(&disp_drv);

    disp_drv.hor_res = lcddev.width;
    disp_drv.ver_res = lcddev.height;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf;

    lv_disp_drv_register(&disp_drv);
}

void lv_port_indev_init(void) {
    static lv_indev_drv_t indev_drv;

    LOG_I("TOUCH", "Initializing GT9xxx touch controller...");
    touch_ready = gt9147_init();
    
    if (touch_ready) {
        LOG_I("TOUCH", "GT9xxx initialized successfully");
    } else {
        LOG_E("TOUCH", "GT9xxx initialization failed");
    }

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    lv_indev_drv_register(&indev_drv);
    
    LOG_I("TOUCH", "LVGL touch input device registered");
}
