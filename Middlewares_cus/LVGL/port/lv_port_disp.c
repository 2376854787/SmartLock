#include "lcd.h"
#include "lvgl.h"

/* Keep draw buffer fixed-size to fit F407 RAM. */
#define BUF_SIZE_BYTES (24U * 1024U)

static uint8_t buf_1[BUF_SIZE_BYTES];
static volatile uint32_t s_flush_count = 0;

static void my_disp_flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    const uint16_t x1 = area->x1;
    const uint16_t y1 = area->y1;
    const uint16_t x2 = area->x2;
    const uint16_t y2 = area->y2;

    lcd_color_fill(x1, y1, x2, y2, (uint16_t*)px_map);
    s_flush_count++;
    lv_display_flush_ready(disp);
}

void lv_port_disp_init(void) {
    lcd_init();

    lv_display_t* disp = lv_display_create(lcddev.width, lcddev.height);
    if (disp == NULL) {
        return;
    }

    lv_display_set_buffers(disp, buf_1, NULL, sizeof(buf_1), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, my_disp_flush);
}

uint32_t lv_port_disp_get_flush_count(void) {
    return s_flush_count;
}
