#include "board_gpio_ids.h"
#include "disp_config.h"
#include "gt911.h"
#include "lvgl.h"


/* -------------------------------------------------------------------------
 * 静态变量定义
 * ------------------------------------------------------------------------- */
static gt911_dev_t s_lv_gt911_dev; /* GT911 设备实例 */

/* 缓存最后的坐标，防止释放时坐标归零导致光标跳动 */
static int16_t last_x = 0;
static int16_t last_y = 0;

/* -------------------------------------------------------------------------
 * 函数声明
 * ------------------------------------------------------------------------- */
static void my_touchpad_read(lv_indev_t* indev, lv_indev_data_t* data);

/* -------------------------------------------------------------------------
 * LVGL 输入设备初始化函数
 * ------------------------------------------------------------------------- */
void lv_port_indev_init(void) {
    /* 1. 初始化 GT911 硬件 */
    const gt911_cfg_t cfg = {.gpio_id_scl  = HAL_GPIO_ID_CT_SCL,
                             .gpio_id_sda  = HAL_GPIO_ID_CT_SDA,
                             .gpio_id_rst  = HAL_GPIO_ID_CT_RST,
                             .gpio_id_int  = HAL_GPIO_ID_CT_INT,
                             .i2c_addr     = GT911_ADDR_HIGH,
                             .max_x        = DISP_PHYS_W, /* GT911 始终用物理分辨率 480 */
                             .max_y        = DISP_PHYS_H, /* GT911 始终用物理分辨率 800 */
                             .refresh_rate = 0};

    const ret_code_t ret  = gt911_init(&s_lv_gt911_dev, &cfg);
    if (ret != RET_OK) {
        return;
    }

    /* 2. 注册 LVGL 输入设备 (v9 写法) */
    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, my_touchpad_read);
}

/* -------------------------------------------------------------------------
 * LVGL 读取回调
 *
 * GT911 报告的坐标始终是物理竖屏方向 (X:0~479, Y:0~799)。
 * 通过 DISP_TOUCH_TRANSFORM 宏自动映射到当前显示方向。
 * ------------------------------------------------------------------------- */
static void my_touchpad_read(lv_indev_t* indev, lv_indev_data_t* data) {
    gt911_touch_data_t touch_data;

    const ret_code_t ret = gt911_read_touch(&s_lv_gt911_dev, &touch_data);

    if (ret == RET_OK && touch_data.count > 0) {
        data->state         = LV_INDEV_STATE_PRESSED;

        /* 获取原始坐标 (物理竖屏方向) */
        const int16_t raw_x = touch_data.points[0].x;
        const int16_t raw_y = touch_data.points[0].y;

        /* 根据 DISP_ORIENTATION 自动变换到 LVGL 坐标系 */
        DISP_TOUCH_TRANSFORM(raw_x, raw_y, &last_x, &last_y);

        data->point.x = last_x;
        data->point.y = last_y;
    } else {
        data->state   = LV_INDEV_STATE_RELEASED;
        data->point.x = last_x;
        data->point.y = last_y;
    }
}