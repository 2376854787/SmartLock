#include "lvgl.h"
#include "gt911.h"
#include "board_gpio_ids.h"

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
static void my_touchpad_read(lv_indev_t * indev, lv_indev_data_t * data);

/* -------------------------------------------------------------------------
 * LVGL 输入设备初始化函数
 * ------------------------------------------------------------------------- */
void lv_port_indev_init(void)
{
    /* 1. 初始化 GT911 硬件 */
    /* 配置结构体参考了你的 touch_test_task.c */
    const gt911_cfg_t cfg = {
        .gpio_id_scl  = HAL_GPIO_ID_CT_SCL,
        .gpio_id_sda  = HAL_GPIO_ID_CT_SDA,
        .gpio_id_rst  = HAL_GPIO_ID_CT_RST,
        .gpio_id_int  = HAL_GPIO_ID_CT_INT,
        .i2c_addr     = GT911_ADDR_HIGH, /* 或 GT911_ADDR_LOW，取决于你的原理图连接 */
        .max_x        = 480,             /* 根据屏幕实际分辨率设置 */
        .max_y        = 800,             /* 根据屏幕实际分辨率设置 */
        .refresh_rate = 0                /* 0表示保持默认，或者设置100Hz */
    };

    const ret_code_t ret = gt911_init(&s_lv_gt911_dev, &cfg);
    if (ret != RET_OK) {
        /* 初始化失败处理，例如打印日志 */
        // LOG_E("LVGL", "Touch init failed: 0x%x", ret);
        return;
    }

    /* 2. 注册 LVGL 输入设备 (v9 写法) */
    lv_indev_t * indev = lv_indev_create();           /* 创建输入设备 */
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);  /* 设置为指针类型(触摸/鼠标) */
    lv_indev_set_read_cb(indev, my_touchpad_read);    /* 设置读取回调函数 */
}

/* -------------------------------------------------------------------------
 * LVGL 读取回调 (每隔几十毫秒由 lv_timer_handler 调用)
 * ------------------------------------------------------------------------- */
static void my_touchpad_read(lv_indev_t * indev, lv_indev_data_t * data)
{
    gt911_touch_data_t touch_data;

    /* 调用你的驱动读取坐标 */
    const ret_code_t ret = gt911_read_touch(&s_lv_gt911_dev, &touch_data);

    /* 逻辑判断：
     * 1. 读取成功 (RET_OK)
     * 2. 触点数量 > 0 (gt911.c 解析出了有效点)
     */
    if (ret == RET_OK && touch_data.count > 0) {
        /* 有触摸按下 */
        data->state = LV_INDEV_STATE_PRESSED;

        /* 获取第一个触点的坐标 (LVGL 默认主要处理单点，多点需手势库支持) */
        last_x = touch_data.points[0].x;
        last_y = touch_data.points[0].y;
        
        /* --- 坐标修正区域 --- 
         * 如果发现触摸方向和屏幕显示方向不一致，在这里调整
         * 例如：如果屏幕是横屏(800x480)，但触摸报的是竖屏(480x800)
         */
        // data->point.x = last_y;
        // data->point.y = 480 - last_x; 
        
        /* 默认直接传递 */
        data->point.x = last_x;
        data->point.y = last_y;
    } 
    else {
        /* 无触摸或释放 */
        data->state = LV_INDEV_STATE_RELEASED;
        
        /* 即使释放，也要传递最后的坐标，否则光标可能会跳到 (0,0) */
        data->point.x = last_x;
        data->point.y = last_y;
    }
}