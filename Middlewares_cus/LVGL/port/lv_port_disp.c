#include "disp_config.h"
#include "lcd.h"
#include "lvgl.h"

/* Keep draw buffer fixed-size to fit F407 RAM. */
#define BUF_SIZE_BYTES (24U * 1024U)

static uint8_t buf_1[BUF_SIZE_BYTES];
static volatile uint32_t s_flush_count = 0;

#if DISP_FLUSH_FLIP180
/**
 * @brief 原地反转 uint16_t 数组 (实现 180° 像素翻转)
 *
 * 将第一个像素与最后一个交换、第二个与倒数第二个交换……
 * 效果等价于对矩形区域做 180° 旋转。
 */
static void reverse_buf16(uint16_t* buf, uint32_t len) {
    uint32_t i = 0, j = len - 1;
    while (i < j) {
        uint16_t tmp = buf[i];
        buf[i]       = buf[j];
        buf[j]       = tmp;
        i++;
        j--;
    }
}
#endif

static void my_disp_flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    uint16_t x1 = area->x1;
    uint16_t y1 = area->y1;
    uint16_t x2 = area->x2;
    uint16_t y2 = area->y2;

#if DISP_FLUSH_FLIP180
    /* 180°/270° 模式: 反转坐标 + 反转像素缓冲区 */
    const uint16_t w            = lcddev.width;
    const uint16_t h            = lcddev.height;
    const uint16_t new_x1       = w - 1 - x2;
    const uint16_t new_y1       = h - 1 - y2;
    const uint16_t new_x2       = w - 1 - x1;
    const uint16_t new_y2       = h - 1 - y1;

    const uint32_t total_pixels = (uint32_t)(x2 - x1 + 1) * (y2 - y1 + 1);
    reverse_buf16((uint16_t*)px_map, total_pixels);

    x1 = new_x1;
    y1 = new_y1;
    x2 = new_x2;
    y2 = new_y2;
#endif

    lcd_color_fill(x1, y1, x2, y2, (uint16_t*)px_map);
    s_flush_count++;
    lv_display_flush_ready(disp);
}

void lv_port_disp_init(void) {
    lcd_init();

    /* 覆盖 lcd_init() 中硬编码的 lcd_display_dir(0) */
    lcd_display_dir(DISP_LCD_DIR);
    lcd_clear(0x0000); /* 清屏为黑色，避免白色闪烁 */

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
