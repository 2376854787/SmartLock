/**
 * @file    disp_config.h
 * @brief   显示方向 & 触控映射 统一配置
 *
 * 修改 DISP_ORIENTATION 即可同时控制 LCD 显示方向和 GT911 触控坐标映射。
 * 只需改这一个宏，无需修改其他文件。
 *
 * 硬件: 正点原子探索者 + 4.3寸 NT35510 TFT LCD
 * 物理分辨率: 480(W) × 800(H), GT911 触控
 *
 * 实现方式:
 *   0°/90°  → lcd_display_dir(0/1) 硬件方向
 *   180°/270° → 在 flush 回调中做 180° 像素翻转
 */

#ifndef DISP_CONFIG_H
#define DISP_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * 用户配置区 - 只需修改这里
 * ========================================================================== */

/**
 * @brief 显示方向配置 (4 方向可选)
 *
 * DISP_DIR_0   : 竖屏正向     480×800  连接器在下方
 * DISP_DIR_90  : 横屏顺时针    800×480  连接器在右侧
 * DISP_DIR_180 : 竖屏倒置     480×800  连接器在上方
 * DISP_DIR_270 : 横屏逆时针    800×480  连接器在左侧
 */
#define DISP_DIR_0   0
#define DISP_DIR_90  1
#define DISP_DIR_180 2
#define DISP_DIR_270 3

#define DISP_ORIENTATION DISP_DIR_270

/* ==========================================================================
 * 派生宏 - 请勿手动修改
 * ========================================================================== */

/* NT35510 物理分辨率 (竖屏方向) */
#define DISP_PHYS_W 480u
#define DISP_PHYS_H 800u

/* LCD 硬件方向: 0=竖屏, 1=横屏 */
#if (DISP_ORIENTATION == DISP_DIR_0) || (DISP_ORIENTATION == DISP_DIR_180)
#define DISP_LCD_DIR 0           /* 竖屏 */
#define DISP_HOR_RES DISP_PHYS_W /* 480 */
#define DISP_VER_RES DISP_PHYS_H /* 800 */
#elif (DISP_ORIENTATION == DISP_DIR_90) || (DISP_ORIENTATION == DISP_DIR_270)
#define DISP_LCD_DIR 1           /* 横屏 */
#define DISP_HOR_RES DISP_PHYS_H /* 800 */
#define DISP_VER_RES DISP_PHYS_W /* 480 */
#else
#error "DISP_ORIENTATION must be DISP_DIR_0/90/180/270"
#endif

/* 是否需要在 flush 中做 180° 翻转 */
#if (DISP_ORIENTATION == DISP_DIR_180) || (DISP_ORIENTATION == DISP_DIR_270)
#define DISP_FLUSH_FLIP180 1
#else
#define DISP_FLUSH_FLIP180 0
#endif

/* GT911 触控始终使用物理分辨率初始化 */
#define DISP_TOUCH_PHYS_X DISP_PHYS_W
#define DISP_TOUCH_PHYS_Y DISP_PHYS_H

/**
 * @brief  将 GT911 原始坐标转换为当前显示方向的 LVGL 坐标
 *
 * GT911 物理坐标系固定为竖屏方向 (原点左上, X:0~479, Y:0~799)。
 */
#if (DISP_ORIENTATION == DISP_DIR_0)
/* 竖屏正向: 直接透传 */
#define DISP_TOUCH_TRANSFORM(raw_x, raw_y, lv_x, lv_y) \
    do {                                               \
        *(lv_x) = (raw_x);                             \
        *(lv_y) = (raw_y);                             \
    } while (0)

#elif (DISP_ORIENTATION == DISP_DIR_90)
/* 横屏顺时针90° */
#define DISP_TOUCH_TRANSFORM(raw_x, raw_y, lv_x, lv_y)    \
    do {                                                  \
        *(lv_x) = (int16_t)((DISP_PHYS_H - 1) - (raw_y)); \
        *(lv_y) = (raw_x);                                \
    } while (0)

#elif (DISP_ORIENTATION == DISP_DIR_180)
/* 竖屏倒置180° */
#define DISP_TOUCH_TRANSFORM(raw_x, raw_y, lv_x, lv_y)    \
    do {                                                  \
        *(lv_x) = (int16_t)((DISP_PHYS_W - 1) - (raw_x)); \
        *(lv_y) = (int16_t)((DISP_PHYS_H - 1) - (raw_y)); \
    } while (0)

#elif (DISP_ORIENTATION == DISP_DIR_270)
/* 横屏逆时针270° */
#define DISP_TOUCH_TRANSFORM(raw_x, raw_y, lv_x, lv_y)    \
    do {                                                  \
        *(lv_x) = (raw_y);                                \
        *(lv_y) = (int16_t)((DISP_PHYS_W - 1) - (raw_x)); \
    } while (0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* DISP_CONFIG_H */
