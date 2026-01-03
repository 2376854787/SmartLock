#include "ui_lock.h"

#include "lvgl.h"

/* UI：多模态智能门锁（开发阶段）
 *
 * 页面流程：
 * - Home：首页时间/日期/天气（当前为占位，后续可接 RTC/网络）
 * - Choose：选择开锁方式（指纹 / RFID / 密码）
 * - 每种方式包含：
 *   - Unlock：验证页
 *   - Manage：管理页（增/删/清空等 CRUD，便于开发调试）
 *
 * 并发模型：
 * - LVGL 必须在 LVGL handler 任务中访问；后台工作（AS608/RC522）放在 worker task 里执行，
 *   再通过 `lv_async_call()` 把结果回传到 UI。
 * - 后续如果加入联网/时间刷新，也遵循同样模式：任务里做耗时工作，UI 更新投递回 LVGL 线程。
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "as608_port.h"
#include "as608_service.h"
#include "lock_data.h"
#include "log.h"
#include "lvgl_task.h"
#include "rc522_my.h"
#include "usart.h"

typedef struct {
    lv_color_t bg;
    lv_color_t card;
    lv_color_t card2;
    lv_color_t text;
    lv_color_t sub;
    lv_color_t accent;
} ui_palette_t;

/* 根据交互需求：首页与子页面使用不同配色方案。 */
static const ui_palette_t s_pal_home = {
    .bg = LV_COLOR_MAKE(0x0B, 0x10, 0x20),
    .card = LV_COLOR_MAKE(0x14, 0x1B, 0x2D),
    .card2 = LV_COLOR_MAKE(0x0F, 0x16, 0x28),
    .text = LV_COLOR_MAKE(0xE8, 0xEE, 0xF8),
    .sub = LV_COLOR_MAKE(0x93, 0xA4, 0xC7),
    .accent = LV_COLOR_MAKE(0x4C, 0x7D, 0xFF),
};

static const ui_palette_t s_pal_sub = {
    .bg = LV_COLOR_MAKE(0x0B, 0x0D, 0x12),
    .card = LV_COLOR_MAKE(0x15, 0x1A, 0x23),
    .card2 = LV_COLOR_MAKE(0x10, 0x15, 0x1D),
    .text = LV_COLOR_MAKE(0xE9, 0xEF, 0xFA),
    .sub = LV_COLOR_MAKE(0xA0, 0xAF, 0xC6),
    .accent = LV_COLOR_MAKE(0x1F, 0xC8, 0xA8),
};

static const ui_palette_t *s_pal = &s_pal_sub;

static void ui_use_palette(const ui_palette_t *pal)
{
    if (pal) s_pal = pal;
}

static lv_color_t c_bg(void) { return s_pal->bg; }
static lv_color_t c_card(void) { return s_pal->card; }
static lv_color_t c_card2(void) { return s_pal->card2; }
static lv_color_t c_text(void) { return s_pal->text; }
static lv_color_t c_sub(void) { return s_pal->sub; }
static lv_color_t c_accent(void) { return s_pal->accent; }
static lv_color_t c_good(void) { return lv_color_hex(0x1FD07A); }
static lv_color_t c_bad(void) { return lv_color_hex(0xFF4D6D); }
static lv_color_t c_warn(void) { return lv_color_hex(0xFFD166); }

static void style_screen(lv_obj_t *scr)
{
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, c_bg(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
}

static void nav_to(lv_obj_t *scr)
{
    if (scr == NULL) return;
    lv_scr_load(scr);
}

/* ================= Toast（提示条） ================= */

typedef struct {
    lv_obj_t *root;
} toast_ctx_t;

static void toast_close_cb(lv_timer_t *t)
{
    toast_ctx_t *ctx = (toast_ctx_t *)t->user_data;
    if (ctx && ctx->root) {
        lv_obj_del(ctx->root);
    }
    lv_timer_del(t);
    if (ctx) vPortFree(ctx);
}

static void ui_toast(const char *text, lv_color_t color, uint32_t ms)
{
    toast_ctx_t *ctx = (toast_ctx_t *)pvPortMalloc(sizeof(toast_ctx_t));
    if (!ctx) return;

    ctx->root = lv_obj_create(lv_layer_top());
    lv_obj_clear_flag(ctx->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ctx->root, 520, 72);
    lv_obj_align(ctx->root, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_set_style_radius(ctx->root, 22, 0);
    lv_obj_set_style_bg_color(ctx->root, c_card(), 0);
    lv_obj_set_style_border_width(ctx->root, 1, 0);
    lv_obj_set_style_border_color(ctx->root, lv_color_hex(0x233152), 0);
    lv_obj_set_style_shadow_width(ctx->root, 18, 0);
    lv_obj_set_style_shadow_color(ctx->root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(ctx->root, LV_OPA_30, 0);

    lv_obj_t *l = lv_label_create(ctx->root);
    lv_label_set_text(l, text ? text : "");
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_center(l);

    (void)lv_timer_create(toast_close_cb, ms ? ms : 1200, ctx);
}

/* ================= 弹窗（Modal/Popup） ================= */

typedef struct {
    lv_obj_t *overlay;
    lv_obj_t *card;
} ui_modal_ctx_t;

static void modal_close_cb(lv_timer_t *t)
{
    ui_modal_ctx_t *ctx = (ui_modal_ctx_t *)t->user_data;
    if (ctx && ctx->overlay) lv_obj_del(ctx->overlay);
    lv_timer_del(t);
    if (ctx) vPortFree(ctx);
}

static void ui_modal_result(const char *title, const char *detail, bool ok, uint32_t ms)
{
    ui_modal_ctx_t *ctx = (ui_modal_ctx_t *)pvPortMalloc(sizeof(ui_modal_ctx_t));
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));

    ctx->overlay = lv_obj_create(lv_layer_top());
    lv_obj_clear_flag(ctx->overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ctx->overlay, lv_pct(100), lv_pct(100));
    lv_obj_center(ctx->overlay);
    lv_obj_set_style_bg_color(ctx->overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->overlay, LV_OPA_60, 0);
    lv_obj_set_style_border_width(ctx->overlay, 0, 0);

    ctx->card = lv_obj_create(ctx->overlay);
    lv_obj_clear_flag(ctx->card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ctx->card, 560, 260);
    lv_obj_center(ctx->card);
    lv_obj_set_style_radius(ctx->card, 26, 0);
    lv_obj_set_style_bg_color(ctx->card, c_card(), 0);
    lv_obj_set_style_bg_opa(ctx->card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ctx->card, 1, 0);
    lv_obj_set_style_border_color(ctx->card, lv_color_hex(0x24304A), 0);
    lv_obj_set_style_shadow_width(ctx->card, 26, 0);
    lv_obj_set_style_shadow_color(ctx->card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(ctx->card, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(ctx->card, 22, 0);

    lv_obj_t *icon = lv_label_create(ctx->card);
    lv_label_set_text(icon, ok ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(icon, ok ? c_good() : c_bad(), 0);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_48, 0);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, -6);

    lv_obj_t *t = lv_label_create(ctx->card);
    lv_label_set_text(t, title ? title : "");
    lv_obj_set_style_text_color(t, c_text(), 0);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 88);

    lv_obj_t *d = lv_label_create(ctx->card);
    lv_label_set_text(d, detail ? detail : "");
    lv_obj_set_style_text_color(d, c_sub(), 0);
    lv_obj_set_style_text_font(d, &lv_font_montserrat_16, 0);
    lv_obj_align(d, LV_ALIGN_TOP_MID, 0, 128);

    (void)lv_timer_create(modal_close_cb, ms ? ms : 1500, ctx);
}

typedef void (*ui_keypad_ok_cb_t)(const char *text, void *user_ctx);

typedef struct {
    lv_obj_t *overlay;
    lv_obj_t *card;
    lv_obj_t *ta;
    ui_keypad_ok_cb_t ok_cb;
    void *user_ctx;
} ui_keypad_ctx_t;

static void keypad_close(ui_keypad_ctx_t *ctx)
{
    if (!ctx) return;
    if (ctx->overlay) lv_obj_del(ctx->overlay);
    vPortFree(ctx);
}

static void keypad_btnm_cb(lv_event_t *e)
{
    lv_obj_t *m = lv_event_get_target(e);
    ui_keypad_ctx_t *ctx = (ui_keypad_ctx_t *)lv_event_get_user_data(e);
    if (!ctx) return;

    const char *txt = lv_btnmatrix_get_btn_text(m, lv_btnmatrix_get_selected_btn(m));
    if (!txt) return;

    if (strcmp(txt, "OK") == 0) {
        const char *val = lv_textarea_get_text(ctx->ta);
        if (ctx->ok_cb) ctx->ok_cb(val ? val : "", ctx->user_ctx);
        keypad_close(ctx);
        return;
    }
    if (strcmp(txt, "Cancel") == 0) {
        keypad_close(ctx);
        return;
    }
    if (strcmp(txt, "Del") == 0) {
        lv_textarea_del_char(ctx->ta);
        return;
    }
    if (strcmp(txt, "Clear") == 0) {
        lv_textarea_set_text(ctx->ta, "");
        return;
    }

    /* digits */
    lv_textarea_add_text(ctx->ta, txt);
}

static void ui_popup_keypad_num(const char *title, const char *initial, uint8_t max_len,
                                ui_keypad_ok_cb_t ok_cb, void *user_ctx)
{
    ui_keypad_ctx_t *ctx = (ui_keypad_ctx_t *)pvPortMalloc(sizeof(ui_keypad_ctx_t));
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
    ctx->ok_cb = ok_cb;
    ctx->user_ctx = user_ctx;

    ctx->overlay = lv_obj_create(lv_layer_top());
    lv_obj_clear_flag(ctx->overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ctx->overlay, lv_pct(100), lv_pct(100));
    lv_obj_center(ctx->overlay);
    lv_obj_set_style_bg_color(ctx->overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ctx->overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(ctx->overlay, 0, 0);

    ctx->card = lv_obj_create(ctx->overlay);
    lv_obj_clear_flag(ctx->card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ctx->card, 560, 380);
    lv_obj_center(ctx->card);
    lv_obj_set_style_radius(ctx->card, 26, 0);
    lv_obj_set_style_bg_color(ctx->card, c_card(), 0);
    lv_obj_set_style_bg_opa(ctx->card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ctx->card, 1, 0);
    lv_obj_set_style_border_color(ctx->card, lv_color_hex(0x24304A), 0);
    lv_obj_set_style_pad_all(ctx->card, 18, 0);
    lv_obj_set_style_pad_gap(ctx->card, 12, 0);

    lv_obj_t *hdr = lv_label_create(ctx->card);
    lv_label_set_text(hdr, title ? title : "");
    lv_obj_set_style_text_color(hdr, c_text(), 0);
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_20, 0);
    lv_obj_align(hdr, LV_ALIGN_TOP_LEFT, 0, 0);

    ctx->ta = lv_textarea_create(ctx->card);
    lv_obj_set_size(ctx->ta, 520, 56);
    lv_obj_align(ctx->ta, LV_ALIGN_TOP_LEFT, 0, 46);
    lv_textarea_set_one_line(ctx->ta, true);
    lv_textarea_set_password_mode(ctx->ta, false);
    lv_textarea_set_max_length(ctx->ta, max_len ? max_len : 6);
    lv_obj_set_style_radius(ctx->ta, 18, 0);
    lv_obj_set_style_bg_color(ctx->ta, c_card2(), 0);
    lv_obj_set_style_text_color(ctx->ta, c_text(), 0);
    lv_obj_set_style_border_width(ctx->ta, 1, 0);
    lv_obj_set_style_border_color(ctx->ta, lv_color_hex(0x24304A), 0);
    if (initial) lv_textarea_set_text(ctx->ta, initial);

    static const char *map[] = {
        "1", "2", "3", "Del", "\n",
        "4", "5", "6", "Clear", "\n",
        "7", "8", "9", "Cancel", "\n",
        "0", "OK", "", "", ""
    };

    lv_obj_t *m = lv_btnmatrix_create(ctx->card);
    lv_btnmatrix_set_map(m, map);
    lv_obj_set_size(m, 520, 240);
    lv_obj_align(m, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_event_cb(m, keypad_btnm_cb, LV_EVENT_VALUE_CHANGED, ctx);
    lv_obj_set_style_bg_opa(m, LV_OPA_0, 0);
    lv_obj_set_style_border_width(m, 0, 0);
    lv_obj_set_style_pad_all(m, 0, 0);
    lv_obj_set_style_pad_gap(m, 10, 0);

    lv_obj_set_style_radius(m, 18, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(m, c_card2(), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(m, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_width(m, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(m, lv_color_hex(0x24304A), LV_PART_ITEMS);
    lv_obj_set_style_text_color(m, c_text(), LV_PART_ITEMS);
    lv_obj_set_style_text_font(m, &lv_font_montserrat_16, LV_PART_ITEMS);
}

/* ================= 通用 UI 组件 ================= */

static lv_obj_t *ui_make_topbar(lv_obj_t *parent, const char *title, lv_event_cb_t back_cb)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_height(bar, 64);
    lv_obj_set_width(bar, lv_pct(100));
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, c_bg(), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, 18, 0);
    lv_obj_set_style_pad_ver(bar, 10, 0);

    lv_obj_t *btn = lv_btn_create(bar);
    lv_obj_set_size(btn, 44, 44);
    lv_obj_align(btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_set_style_bg_color(btn, c_card2(), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, LV_SYMBOL_LEFT);
    lv_obj_center(icon);
    lv_obj_set_style_text_color(icon, c_text(), 0);

    lv_obj_t *lbl = lv_label_create(bar);
    lv_label_set_text(lbl, title);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 58, 0);
    lv_obj_set_style_text_color(lbl, c_text(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);

    return bar;
}

static lv_obj_t *ui_make_card_button(lv_obj_t *parent, const char *title, const char *subtitle)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_style_radius(btn, 20, 0);
    lv_obj_set_style_bg_color(btn, c_card(), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x233152), 0);
    lv_obj_set_style_pad_all(btn, 18, 0);
    lv_obj_set_style_shadow_width(btn, 18, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_20, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(btn);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, c_text(), 0);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *s = lv_label_create(btn);
    lv_label_set_text(s, subtitle);
    lv_obj_set_style_text_color(s, c_sub(), 0);
    lv_obj_set_style_text_font(s, &lv_font_montserrat_14, 0);
    lv_obj_align(s, LV_ALIGN_TOP_LEFT, 0, 30);

    return btn;
}

static void set_label(lv_obj_t *lbl, const char *text, lv_color_t color)
{
    if (!lbl) return;
    lv_label_set_text(lbl, text ? text : "");
    lv_obj_set_style_text_color(lbl, color, 0);
}

/* ================= 页面（Screens） ================= */

static lv_obj_t *s_home = NULL;
static lv_obj_t *s_choose = NULL;
static lv_obj_t *s_fp_unlock = NULL;
static lv_obj_t *s_fp_manage = NULL;
static lv_obj_t *s_rfid_unlock = NULL;
static lv_obj_t *s_rfid_manage = NULL;
static lv_obj_t *s_pin_unlock = NULL;

static lv_obj_t *s_home_time = NULL;
static lv_obj_t *s_home_date = NULL;
static lv_obj_t *s_home_weather = NULL;

/* -------------------- Epoch 转本地时间 -------------------- */

static const uint8_t s_days_in_month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
static const char *s_weekday_names[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static bool is_leap(uint32_t year)
{
    return (year % 4u == 0 && year % 100u != 0) || (year % 400u == 0);
}

static void epoch_to_local_time(uint32_t epoch_s, int tz_offset_h,
                                 uint32_t *out_year, uint32_t *out_month, uint32_t *out_day,
                                 uint32_t *out_hour, uint32_t *out_min, uint32_t *out_sec,
                                 uint32_t *out_weekday)
{
    /* 应用时区偏移 */
    int64_t local_s = (int64_t)epoch_s + (int64_t)tz_offset_h * 3600;
    if (local_s < 0) local_s = 0;

    uint32_t days = (uint32_t)(local_s / 86400);
    uint32_t day_secs = (uint32_t)(local_s % 86400);

    /* 1970-01-01 是星期四 (weekday=4) */
    *out_weekday = (days + 4u) % 7u;

    *out_hour = day_secs / 3600u;
    *out_min = (day_secs % 3600u) / 60u;
    *out_sec = day_secs % 60u;

    /* 计算年份 */
    uint32_t year = 1970u;
    for (;;) {
        uint32_t days_in_year = is_leap(year) ? 366u : 365u;
        if (days < days_in_year) break;
        days -= days_in_year;
        year++;
    }
    *out_year = year;

    /* 计算月份和日期 */
    uint32_t month = 1u;
    for (uint32_t m = 0; m < 12u; m++) {
        uint32_t dim = s_days_in_month[m];
        if (m == 1u && is_leap(year)) dim = 29u;
        if (days < dim) {
            month = m + 1u;
            break;
        }
        days -= dim;
    }
    *out_month = month;
    *out_day = days + 1u;
}

static void home_tick_cb(lv_timer_t *t)
{
    (void)t;
    if (lv_scr_act() != s_home) return;

    char time_buf[32];
    char date_buf[48];

    if (lock_time_has_epoch()) {
        /* 有 SNTP 时间：显示真实时间 */
        uint32_t epoch_s = lock_time_now_s();
        uint32_t year, month, day, hour, min, sec, weekday;
        epoch_to_local_time(epoch_s, 8, &year, &month, &day, &hour, &min, &sec, &weekday);

        snprintf(time_buf, sizeof(time_buf), "%02lu:%02lu", (unsigned long)hour, (unsigned long)min);
        snprintf(date_buf, sizeof(date_buf), "%s · %04lu-%02lu-%02lu",
                 s_weekday_names[weekday],
                 (unsigned long)year,
                 (unsigned long)month,
                 (unsigned long)day);
    } else {
        /* 未同步：显示启动后的相对时间 */
        uint32_t sec = (uint32_t)(xTaskGetTickCount() / configTICK_RATE_HZ);
        uint32_t hh = (sec / 3600u) % 24u;
        uint32_t mm = (sec / 60u) % 60u;

        snprintf(time_buf, sizeof(time_buf), "%02lu:%02lu", (unsigned long)hh, (unsigned long)mm);
        snprintf(date_buf, sizeof(date_buf), "Syncing time...");
    }

    lv_label_set_text(s_home_time, time_buf);
    lv_label_set_text(s_home_date, date_buf);
    lv_label_set_text(s_home_weather, "SmartLock · Ready");
}

static void home_tap_cb(lv_event_t *e)
{
    (void)e;
    nav_to(s_choose);
}

static void build_home(void)
{
    s_home = lv_obj_create(NULL);
    style_screen(s_home);

    lv_obj_t *bg = lv_obj_create(s_home);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(bg, lv_pct(100), lv_pct(100));
    lv_obj_center(bg);
    lv_obj_set_style_border_width(bg, 0, 0);
    lv_obj_set_style_bg_color(bg, lv_color_hex(0x0B1020), 0);
    lv_obj_set_style_bg_grad_color(bg, lv_color_hex(0x141B2D), 0);
    lv_obj_set_style_bg_grad_dir(bg, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_pad_all(bg, 0, 0);

    lv_obj_t *orb1 = lv_obj_create(bg);
    lv_obj_clear_flag(orb1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(orb1, 220, 220);
    lv_obj_align(orb1, LV_ALIGN_TOP_RIGHT, 90, -60);
    lv_obj_set_style_radius(orb1, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(orb1, c_accent(), 0);
    lv_obj_set_style_bg_opa(orb1, LV_OPA_20, 0);
    lv_obj_set_style_border_width(orb1, 0, 0);

    lv_obj_t *orb2 = lv_obj_create(bg);
    lv_obj_clear_flag(orb2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(orb2, 160, 160);
    lv_obj_align(orb2, LV_ALIGN_BOTTOM_LEFT, -60, 60);
    lv_obj_set_style_radius(orb2, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(orb2, lv_color_hex(0x1FD07A), 0);
    lv_obj_set_style_bg_opa(orb2, LV_OPA_20, 0);
    lv_obj_set_style_border_width(orb2, 0, 0);

    s_home_time = lv_label_create(bg);
    lv_label_set_text(s_home_time, "12:34");
    lv_obj_set_style_text_color(s_home_time, c_text(), 0);
    lv_obj_set_style_text_font(s_home_time, &lv_font_montserrat_48, 0);
    lv_obj_align(s_home_time, LV_ALIGN_CENTER, 0, -40);

    s_home_date = lv_label_create(bg);
    lv_label_set_text(s_home_date, "Mon · 2026-01-01");
    lv_obj_set_style_text_color(s_home_date, c_sub(), 0);
    lv_obj_set_style_text_font(s_home_date, &lv_font_montserrat_16, 0);
    lv_obj_align_to(s_home_date, s_home_time, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    s_home_weather = lv_label_create(bg);
    lv_label_set_text(s_home_weather, "Sunny · 24°C  |  AQI 42");
    lv_obj_set_style_text_color(s_home_weather, c_sub(), 0);
    lv_obj_set_style_text_font(s_home_weather, &lv_font_montserrat_16, 0);
    lv_obj_align_to(s_home_weather, s_home_date, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    lv_obj_t *hint = lv_label_create(bg);
    lv_label_set_text(hint, "Tap anywhere to unlock");
    lv_obj_set_style_text_color(hint, lv_color_hex(0xB8C6E6), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -26);

    lv_obj_t *tap = lv_btn_create(bg);
    lv_obj_set_size(tap, lv_pct(100), lv_pct(100));
    lv_obj_align(tap, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(tap, LV_OPA_0, 0);
    lv_obj_set_style_border_width(tap, 0, 0);
    lv_obj_add_event_cb(tap, home_tap_cb, LV_EVENT_CLICKED, NULL);

    (void)lv_timer_create(home_tick_cb, 1000, NULL);
}

/* 前置声明：builder */
static void build_home(void);
static void build_choose(void);
static void build_fp_unlock(void);
static void build_fp_manage(void);
static void build_rfid_unlock(void);
static void build_rfid_manage(void);
static void build_pin_unlock(void);
static void build_pin_manage(void);

/* ================= 密码（PIN） ================= */

static lv_obj_t *s_pin_status = NULL;
static lv_obj_t *s_pin_ta = NULL;
static lv_obj_t *s_pin_manage = NULL;
static lv_obj_t *s_pin_manage_status = NULL;
static lv_obj_t *s_pin_list = NULL;
static lv_obj_t *s_pin_page_lbl = NULL;
static lv_obj_t *s_pin_sel_lbl = NULL;
static uint16_t s_pin_page = 0;
static uint16_t s_pin_sel_mask = 0; /* by current list index */
static char s_pin_add_value[LOCK_PIN_MAX_LEN + 1u];

static void pin_back_to_choose(lv_event_t *e) { (void)e; nav_to(s_choose); }
static void pin_back_to_unlock(lv_event_t *e) { (void)e; nav_to(s_pin_unlock); }
static void pin_manage_refresh_list(void);

static void pin_open_manage_cb(lv_event_t *e)
{
    (void)e;
    s_pin_page = 0;
    s_pin_sel_mask = 0;
    nav_to(s_pin_manage);
    pin_manage_refresh_list();
}

static void pin_btnm_cb(lv_event_t *e)
{
    lv_obj_t *m = lv_event_get_target(e);
    const char *txt = lv_btnmatrix_get_btn_text(m, lv_btnmatrix_get_selected_btn(m));
    if (!txt) return;

    if (strcmp(txt, "OK") == 0) {
        const char *in = lv_textarea_get_text(s_pin_ta);
        uint32_t pin_id = 0;
        if (in && lock_pin_verify(in, &pin_id)) {
            set_label(s_pin_status, "Unlocked", c_good());
            ui_modal_result("Unlocked", "PIN verified", true, 1500);
            lv_textarea_set_text(s_pin_ta, "");
            nav_to(s_home);
        } else {
            set_label(s_pin_status, "Wrong or expired PIN", c_bad());
            ui_modal_result("Access denied", "Wrong or expired PIN", false, 1500);
            lv_textarea_set_text(s_pin_ta, "");
        }
        return;
    }

    if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
        lv_textarea_del_char(s_pin_ta);
        return;
    }

    if (strcmp(txt, "CLR") == 0) {
        lv_textarea_set_text(s_pin_ta, "");
        return;
    }

    if (txt[0] >= '0' && txt[0] <= '9' && txt[1] == '\0') {
        lv_textarea_add_text(s_pin_ta, txt);
    }
}

static void build_pin_unlock(void)
{
    s_pin_unlock = lv_obj_create(NULL);
    style_screen(s_pin_unlock);
    ui_make_topbar(s_pin_unlock, "Password", pin_back_to_choose);

    lv_obj_t *card = lv_obj_create(s_pin_unlock);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(card, 760, 360);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_radius(card, 24, 0);
    lv_obj_set_style_bg_color(card, c_card(), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x24304A), 0);
    lv_obj_set_style_pad_all(card, 22, 0);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "Enter PIN");
    lv_obj_set_style_text_color(title, c_text(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_pin_ta = lv_textarea_create(card);
    lv_obj_set_size(s_pin_ta, 320, 52);
    lv_obj_align(s_pin_ta, LV_ALIGN_TOP_LEFT, 0, 50);
    lv_textarea_set_one_line(s_pin_ta, true);
    lv_textarea_set_password_mode(s_pin_ta, true);
    lv_textarea_set_max_length(s_pin_ta, 6);
    lv_obj_set_style_radius(s_pin_ta, 18, 0);
    lv_obj_set_style_bg_color(s_pin_ta, c_card2(), 0);
    lv_obj_set_style_text_color(s_pin_ta, c_text(), 0);
    lv_obj_set_style_border_width(s_pin_ta, 1, 0);
    lv_obj_set_style_border_color(s_pin_ta, lv_color_hex(0x24304A), 0);

    s_pin_status = lv_label_create(card);
    lv_label_set_text(s_pin_status, "Use Manage to add PINs");
    lv_obj_set_style_text_color(s_pin_status, c_sub(), 0);
    lv_obj_set_style_text_font(s_pin_status, &lv_font_montserrat_14, 0);
    lv_obj_align(s_pin_status, LV_ALIGN_TOP_LEFT, 0, 112);

    static const char *map[] = {
        "1", "2", "3", "\n",
        "4", "5", "6", "\n",
        "7", "8", "9", "\n",
        "CLR", "0", LV_SYMBOL_BACKSPACE, "\n",
        "OK", ""
    };
    lv_obj_t *m = lv_btnmatrix_create(card);
    lv_btnmatrix_set_map(m, map);
    lv_obj_set_size(m, 420, 210);
    lv_obj_align(m, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_event_cb(m, pin_btnm_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_style_bg_opa(m, LV_OPA_0, 0);
    lv_obj_set_style_border_width(m, 0, 0);
    lv_obj_set_style_pad_all(m, 0, 0);
    lv_obj_set_style_pad_gap(m, 10, 0);

    lv_obj_set_style_radius(m, 18, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(m, c_card2(), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(m, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_width(m, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(m, lv_color_hex(0x24304A), LV_PART_ITEMS);
    lv_obj_set_style_text_color(m, c_text(), LV_PART_ITEMS);
    lv_obj_set_style_text_font(m, &lv_font_montserrat_16, LV_PART_ITEMS);

    lv_obj_t *btn_manage = lv_btn_create(card);
    lv_obj_set_size(btn_manage, 180, 56);
    lv_obj_align(btn_manage, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_radius(btn_manage, 18, 0);
    lv_obj_set_style_bg_color(btn_manage, c_card2(), 0);
    lv_obj_set_style_border_width(btn_manage, 1, 0);
    lv_obj_set_style_border_color(btn_manage, lv_color_hex(0x24304A), 0);
    lv_obj_add_event_cb(btn_manage, pin_open_manage_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_manage = lv_label_create(btn_manage);
    lv_label_set_text(lbl_manage, LV_SYMBOL_SETTINGS "  Manage");
    lv_obj_set_style_text_color(lbl_manage, c_text(), 0);
    lv_obj_set_style_text_font(lbl_manage, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_manage);
}

static void pin_manage_refresh_list(void);
static void pin_update_page_labels(uint8_t total);

static void pin_item_set_style(lv_obj_t *btn, bool selected)
{
    if (!btn) return;
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x24304A), 0);
    lv_obj_set_style_bg_color(btn, selected ? c_accent() : c_card2(), 0);
}

static void pin_item_click_cb(lv_event_t *e)
{
    uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    uint8_t total = lock_pin_count();
    if (idx >= total || idx >= 16u) return;

    s_pin_sel_mask ^= (uint16_t)(1u << idx);
    bool selected = (s_pin_sel_mask & (uint16_t)(1u << idx)) != 0u;

    lv_obj_t *btn = lv_event_get_target(e);
    pin_item_set_style(btn, selected);

    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    if (lbl) {
        lock_pin_entry_t entry;
        if (lock_pin_get(idx, &entry)) {
            uint32_t now_s = lock_time_now_s();
            char exp[32];
            if (entry.expires_at_s == 0) {
                strncpy(exp, "never", sizeof(exp));
                exp[sizeof(exp) - 1u] = '\0';
            } else if (lock_pin_is_expired(&entry, now_s)) {
                strncpy(exp, "expired", sizeof(exp));
                exp[sizeof(exp) - 1u] = '\0';
            } else {
                uint32_t left_s = entry.expires_at_s - now_s;
                uint32_t left_m = (left_s + 59u) / 60u;
                snprintf(exp, sizeof(exp), "%lum", (unsigned long)left_m);
            }

            char line[72];
            snprintf(line, sizeof(line), "%s  PIN #%lu  (%s)",
                     selected ? LV_SYMBOL_OK : "  ",
                     (unsigned long)entry.id,
                     exp);
            lv_label_set_text(lbl, line);
            lv_obj_set_style_text_color(lbl, selected ? lv_color_white() : c_text(), 0);
        }
    }

    pin_update_page_labels(total);
}

static void pin_update_page_labels(uint8_t total)
{
    if (s_pin_page_lbl) {
        const uint16_t page_size = 6u;
        uint16_t pages = ((uint16_t)total + page_size - 1u) / page_size;
        if (pages == 0) pages = 1;
        if (s_pin_page >= pages) s_pin_page = pages - 1u;
        char buf[24];
        snprintf(buf, sizeof(buf), "Page %u/%u", (unsigned)(s_pin_page + 1u), (unsigned)pages);
        lv_label_set_text(s_pin_page_lbl, buf);
    }

    if (s_pin_sel_lbl) {
        uint16_t selected = 0;
        for (uint8_t i = 0; i < total && i < 16u; i++) {
            if (s_pin_sel_mask & (uint16_t)(1u << i)) selected++;
        }
        char buf[32];
        snprintf(buf, sizeof(buf), "Selected: %u", (unsigned)selected);
        lv_label_set_text(s_pin_sel_lbl, buf);
    }
}

static void pin_manage_rebuild_list_ui(void)
{
    if (!s_pin_list) return;
    lv_obj_clean(s_pin_list);

    uint8_t total = lock_pin_count();
    const uint16_t page_size = 6u;
    pin_update_page_labels(total);

    uint16_t start = (uint16_t)(s_pin_page * page_size);
    uint16_t end = start + page_size;
    if (end > total) end = total;

    for (uint16_t i = start; i < end; i++) {
        uint8_t idx = (uint8_t)i;
        lock_pin_entry_t entry;
        if (!lock_pin_get(idx, &entry)) continue;

        bool selected = (idx < 16u) && ((s_pin_sel_mask & (uint16_t)(1u << idx)) != 0u);

        uint32_t now_s = lock_time_now_s();
        char exp[32];
        if (entry.expires_at_s == 0) {
            strncpy(exp, "never", sizeof(exp));
            exp[sizeof(exp) - 1u] = '\0';
        } else if (lock_pin_is_expired(&entry, now_s)) {
            strncpy(exp, "expired", sizeof(exp));
            exp[sizeof(exp) - 1u] = '\0';
        } else {
            uint32_t left_s = entry.expires_at_s - now_s;
            uint32_t left_m = (left_s + 59u) / 60u;
            snprintf(exp, sizeof(exp), "%lum", (unsigned long)left_m);
        }

        lv_obj_t *btn = lv_btn_create(s_pin_list);
        lv_obj_set_size(btn, 716, 52);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        pin_item_set_style(btn, selected);
        lv_obj_add_event_cb(btn, pin_item_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)idx);

        lv_obj_t *lbl = lv_label_create(btn);
        char line[72];
        snprintf(line, sizeof(line), "%s  PIN #%lu  (%s)",
                 selected ? LV_SYMBOL_OK : "  ",
                 (unsigned long)entry.id,
                 exp);
        lv_label_set_text(lbl, line);
        lv_obj_set_style_text_color(lbl, selected ? lv_color_white() : c_text(), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 14, 0);
    }

    pin_update_page_labels(total);
}

static void pin_manage_refresh_list(void)
{
    pin_manage_rebuild_list_ui();
}

static void pin_prev_page_cb(lv_event_t *e)
{
    (void)e;
    if (s_pin_page > 0) s_pin_page--;
    pin_manage_rebuild_list_ui();
}

static void pin_next_page_cb(lv_event_t *e)
{
    (void)e;
    const uint16_t page_size = 6u;
    uint8_t total = lock_pin_count();
    uint16_t pages = ((uint16_t)total + page_size - 1u) / page_size;
    if (pages == 0) pages = 1;
    if ((s_pin_page + 1u) < pages) s_pin_page++;
    pin_manage_rebuild_list_ui();
}

static void pin_add_ttl_ok_cb(const char *text, void *user_ctx)
{
    (void)user_ctx;
    uint32_t ttl_min = (uint32_t)atoi(text ? text : "0");
    uint32_t new_id = 0;
    if (!lock_pin_add(s_pin_add_value, ttl_min, NULL, &new_id)) {
        ui_modal_result("PIN add failed", "DB full or invalid", false, 1600);
        set_label(s_pin_manage_status, "Add failed", c_bad());
        return;
    }

    char msg[64];
    snprintf(msg, sizeof(msg), "Added PIN #%lu", (unsigned long)new_id);
    ui_modal_result("PIN added", msg, true, 1400);
    set_label(s_pin_manage_status, msg, c_good());
    s_pin_sel_mask = 0;
    pin_manage_rebuild_list_ui();
}

static void pin_add_pin_ok_cb(const char *text, void *user_ctx)
{
    (void)user_ctx;
    if (!text) text = "";
    size_t len = strlen(text);
    if (len < 4u || len > LOCK_PIN_MAX_LEN) {
        ui_toast("PIN length: 4-6 digits", c_text(), 1200);
        return;
    }
    strncpy(s_pin_add_value, text, sizeof(s_pin_add_value) - 1u);
    s_pin_add_value[sizeof(s_pin_add_value) - 1u] = '\0';

    ui_popup_keypad_num("Validity minutes (0 = never)", "0", 5, pin_add_ttl_ok_cb, NULL);
}

static void pin_add_cb(lv_event_t *e)
{
    (void)e;
    memset(s_pin_add_value, 0, sizeof(s_pin_add_value));
    ui_popup_keypad_num("New PIN (4-6 digits)", "", LOCK_PIN_MAX_LEN, pin_add_pin_ok_cb, NULL);
}

static void pin_delete_selected_cb(lv_event_t *e)
{
    (void)e;
    uint8_t total = lock_pin_count();
    if (total == 0) {
        ui_toast("No PINs", c_text(), 900);
        return;
    }

    uint32_t ids[LOCK_PIN_MAX];
    uint8_t id_count = 0;
    for (uint8_t i = 0; i < total && i < 16u; i++) {
        if ((s_pin_sel_mask & (uint16_t)(1u << i)) == 0u) continue;
        lock_pin_entry_t entry;
        if (lock_pin_get(i, &entry) && id_count < LOCK_PIN_MAX) {
            ids[id_count++] = entry.id;
        }
    }

    if (id_count == 0) {
        ui_toast("No PIN selected", c_text(), 900);
        return;
    }

    for (uint8_t i = 0; i < id_count; i++) {
        (void)lock_pin_remove(ids[i]);
    }

    s_pin_sel_mask = 0;
    pin_manage_rebuild_list_ui();

    char msg[48];
    snprintf(msg, sizeof(msg), "Removed %u PIN(s)", (unsigned)id_count);
    ui_modal_result("PIN updated", msg, true, 1500);
    set_label(s_pin_manage_status, msg, c_good());
}

static void pin_clear_all_cb(lv_event_t *e)
{
    (void)e;
    lock_pin_clear();
    s_pin_sel_mask = 0;
    s_pin_page = 0;
    pin_manage_rebuild_list_ui();
    ui_modal_result("PIN cleared", "All PINs removed", true, 1500);
    set_label(s_pin_manage_status, "All PINs cleared", c_good());
}

static void build_pin_manage(void)
{
    s_pin_manage = lv_obj_create(NULL);
    style_screen(s_pin_manage);
    ui_make_topbar(s_pin_manage, "Password · Manage", pin_back_to_unlock);

    lv_obj_t *card = lv_obj_create(s_pin_manage);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(card, 760, 360);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 12);
    lv_obj_set_style_radius(card, 24, 0);
    lv_obj_set_style_bg_color(card, c_card(), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x24304A), 0);
    lv_obj_set_style_pad_all(card, 22, 0);
    lv_obj_set_style_pad_gap(card, 12, 0);

    lv_obj_t *info = lv_label_create(card);
    lv_label_set_text(info, "Create multiple PINs. Optional expiry uses uptime (dev).");
    lv_obj_set_style_text_color(info, c_sub(), 0);
    lv_obj_set_style_text_font(info, &lv_font_montserrat_14, 0);
    lv_obj_align(info, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *btn_add = lv_btn_create(card);
    lv_obj_set_size(btn_add, 220, 54);
    lv_obj_align(btn_add, LV_ALIGN_TOP_LEFT, 0, 34);
    lv_obj_set_style_radius(btn_add, 18, 0);
    lv_obj_set_style_bg_color(btn_add, c_accent(), 0);
    lv_obj_set_style_border_width(btn_add, 0, 0);
    lv_obj_add_event_cb(btn_add, pin_add_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *la = lv_label_create(btn_add);
    lv_label_set_text(la, LV_SYMBOL_PLUS "  Add PIN");
    lv_obj_set_style_text_color(la, lv_color_white(), 0);
    lv_obj_set_style_text_font(la, &lv_font_montserrat_16, 0);
    lv_obj_center(la);

    lv_obj_t *btn_del = lv_btn_create(card);
    lv_obj_set_size(btn_del, 240, 54);
    lv_obj_align(btn_del, LV_ALIGN_TOP_LEFT, 250, 34);
    lv_obj_set_style_radius(btn_del, 18, 0);
    lv_obj_set_style_bg_color(btn_del, lv_color_hex(0x24304A), 0);
    lv_obj_set_style_border_width(btn_del, 1, 0);
    lv_obj_set_style_border_color(btn_del, lv_color_hex(0x24304A), 0);
    lv_obj_add_event_cb(btn_del, pin_delete_selected_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ld = lv_label_create(btn_del);
    lv_label_set_text(ld, LV_SYMBOL_TRASH "  Delete Selected");
    lv_obj_set_style_text_color(ld, c_text(), 0);
    lv_obj_set_style_text_font(ld, &lv_font_montserrat_16, 0);
    lv_obj_center(ld);

    lv_obj_t *btn_clear = lv_btn_create(card);
    lv_obj_set_size(btn_clear, 220, 54);
    lv_obj_align(btn_clear, LV_ALIGN_TOP_LEFT, 520, 34);
    lv_obj_set_style_radius(btn_clear, 18, 0);
    lv_obj_set_style_bg_color(btn_clear, c_card2(), 0);
    lv_obj_set_style_border_width(btn_clear, 1, 0);
    lv_obj_set_style_border_color(btn_clear, lv_color_hex(0x24304A), 0);
    lv_obj_add_event_cb(btn_clear, pin_clear_all_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lc = lv_label_create(btn_clear);
    lv_label_set_text(lc, LV_SYMBOL_CLOSE "  Clear All");
    lv_obj_set_style_text_color(lc, c_text(), 0);
    lv_obj_set_style_text_font(lc, &lv_font_montserrat_16, 0);
    lv_obj_center(lc);

    s_pin_list = lv_obj_create(card);
    lv_obj_clear_flag(s_pin_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_pin_list, 716, 176);
    lv_obj_align(s_pin_list, LV_ALIGN_TOP_LEFT, 0, 98);
    lv_obj_set_style_bg_opa(s_pin_list, LV_OPA_0, 0);
    lv_obj_set_style_border_width(s_pin_list, 0, 0);
    lv_obj_set_style_pad_all(s_pin_list, 0, 0);
    lv_obj_set_style_pad_gap(s_pin_list, 10, 0);
    lv_obj_set_flex_flow(s_pin_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_pin_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *page_bar = lv_obj_create(card);
    lv_obj_clear_flag(page_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(page_bar, 716, 46);
    lv_obj_align(page_bar, LV_ALIGN_TOP_LEFT, 0, 284);
    lv_obj_set_style_bg_opa(page_bar, LV_OPA_0, 0);
    lv_obj_set_style_border_width(page_bar, 0, 0);
    lv_obj_set_style_pad_all(page_bar, 0, 0);

    lv_obj_t *btn_prev = lv_btn_create(page_bar);
    lv_obj_set_size(btn_prev, 120, 42);
    lv_obj_align(btn_prev, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(btn_prev, 16, 0);
    lv_obj_set_style_bg_color(btn_prev, c_card2(), 0);
    lv_obj_set_style_border_width(btn_prev, 1, 0);
    lv_obj_set_style_border_color(btn_prev, lv_color_hex(0x24304A), 0);
    lv_obj_add_event_cb(btn_prev, pin_prev_page_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lp = lv_label_create(btn_prev);
    lv_label_set_text(lp, LV_SYMBOL_LEFT "  Prev");
    lv_obj_set_style_text_color(lp, c_text(), 0);
    lv_obj_center(lp);

    s_pin_page_lbl = lv_label_create(page_bar);
    lv_label_set_text(s_pin_page_lbl, "Page 1/1");
    lv_obj_set_style_text_color(s_pin_page_lbl, c_sub(), 0);
    lv_obj_set_style_text_font(s_pin_page_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(s_pin_page_lbl, LV_ALIGN_CENTER, 0, 0);

    s_pin_sel_lbl = lv_label_create(page_bar);
    lv_label_set_text(s_pin_sel_lbl, "Selected: 0");
    lv_obj_set_style_text_color(s_pin_sel_lbl, c_sub(), 0);
    lv_obj_set_style_text_font(s_pin_sel_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(s_pin_sel_lbl, LV_ALIGN_RIGHT_MID, -140, 0);

    lv_obj_t *btn_next = lv_btn_create(page_bar);
    lv_obj_set_size(btn_next, 120, 42);
    lv_obj_align(btn_next, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(btn_next, 16, 0);
    lv_obj_set_style_bg_color(btn_next, c_card2(), 0);
    lv_obj_set_style_border_width(btn_next, 1, 0);
    lv_obj_set_style_border_color(btn_next, lv_color_hex(0x24304A), 0);
    lv_obj_add_event_cb(btn_next, pin_next_page_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ln = lv_label_create(btn_next);
    lv_label_set_text(ln, "Next  " LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(ln, c_text(), 0);
    lv_obj_center(ln);

    s_pin_manage_status = lv_label_create(card);
    lv_label_set_text(s_pin_manage_status, "Ready");
    lv_obj_set_style_text_color(s_pin_manage_status, c_sub(), 0);
    lv_obj_set_style_text_font(s_pin_manage_status, &lv_font_montserrat_14, 0);
    lv_obj_align(s_pin_manage_status, LV_ALIGN_BOTTOM_LEFT, 0, -64);

    pin_manage_rebuild_list_ui();
}

/* ================= 指纹（AS608） ================= */

typedef enum {
    FP_OP_VERIFY = 0,
    FP_OP_ENROLL,
    FP_OP_DELETE,
    FP_OP_CLEAR,
    FP_OP_LIST_INDEX,
} fp_op_t;

typedef struct {
    fp_op_t op;
    uint16_t id;
} fp_cmd_t;

typedef struct {
    fp_op_t op;
    uint16_t id;
    as608_svc_rc_t rc;
    as608_status_t st;
    uint16_t found_id;
    uint16_t score;
    uint16_t capacity;
    uint8_t index_bits[128]; /* 4 * 32 bytes = 1024 bits */
} fp_res_t;

static QueueHandle_t s_fp_q = NULL;
static TaskHandle_t s_fp_task = NULL;
static volatile bool s_fp_busy = false;

static lv_obj_t *s_fp_unlock_status = NULL;
static lv_obj_t *s_fp_manage_status = NULL;

/* 指纹管理页 UI 状态 */
static uint16_t s_fp_capacity = 0;
static uint16_t s_fp_target_id = 1;
static uint8_t s_fp_index_bits[128];
static uint8_t s_fp_selected_bits[128];
static uint16_t s_fp_present_ids[1024];
static uint16_t s_fp_present_count = 0;
static uint16_t s_fp_page = 0;

static lv_obj_t *s_fp_id_btn = NULL;
static lv_obj_t *s_fp_id_lbl = NULL;
static lv_obj_t *s_fp_list = NULL;
static lv_obj_t *s_fp_page_lbl = NULL;
static lv_obj_t *s_fp_sel_lbl = NULL;

/* 批量删除（串行执行，保持 AS608 “一次只做一个操作”的语义） */
static bool s_fp_del_batch_active = false;
static uint16_t s_fp_del_batch_ids[64];
static uint8_t s_fp_del_batch_count = 0;
static uint8_t s_fp_del_batch_pos = 0;
static uint8_t s_fp_del_batch_ok = 0;
static uint8_t s_fp_del_batch_fail = 0;

static const char *fp_rc_str(as608_svc_rc_t rc)
{
    switch (rc) {
        case AS608_SVC_OK: return "OK";
        case AS608_SVC_TIMEOUT: return "TIMEOUT";
        case AS608_SVC_NOT_READY: return "NOT_READY";
        case AS608_SVC_ERR: return "ERR";
        default: return "UNK";
    }
}

static const char *fp_st_str(as608_status_t st)
{
    switch (st) {
        case AS608_STATUS_OK: return "OK";
        case AS608_STATUS_NO_FINGERPRINT: return "NO_FINGER";
        case AS608_STATUS_NOT_FOUND: return "NOT_FOUND";
        case AS608_STATUS_NOT_MATCH: return "NOT_MATCH";
        default: return "OTHER";
    }
}

static void fp_item_click_cb(lv_event_t *e);
static void fp_item_long_cb(lv_event_t *e);

static inline bool bit_get_u8(const uint8_t *bits, uint16_t bit_index)
{
    return (bits[bit_index >> 3] & (uint8_t)(1u << (bit_index & 7u))) != 0u;
}

static inline void bit_set_u8(uint8_t *bits, uint16_t bit_index, bool value)
{
    uint8_t *b = &bits[bit_index >> 3];
    uint8_t m = (uint8_t)(1u << (bit_index & 7u));
    if (value) *b |= m;
    else *b &= (uint8_t)~m;
}

static void fp_rebuild_present_ids(void)
{
    s_fp_present_count = 0;

    uint16_t cap = s_fp_capacity;
    if (cap == 0) cap = 300; /* fallback */
    if (cap > 1024u) cap = 1024u;

    for (uint16_t id = 0; id < cap; id++) {
        if (!bit_get_u8(s_fp_index_bits, id)) continue;
        s_fp_present_ids[s_fp_present_count++] = id;
        if (s_fp_present_count >= (uint16_t)(sizeof(s_fp_present_ids) / sizeof(s_fp_present_ids[0]))) break;
    }
}

static uint16_t fp_selected_count(void)
{
    uint16_t count = 0;
    for (uint16_t i = 0; i < s_fp_present_count; i++) {
        if (bit_get_u8(s_fp_selected_bits, s_fp_present_ids[i])) count++;
    }
    return count;
}

static void fp_update_id_label(void)
{
    if (!s_fp_id_lbl) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "ID: %u (tap to edit)", (unsigned)s_fp_target_id);
    lv_label_set_text(s_fp_id_lbl, buf);
}

static void fp_update_page_label(void)
{
    if (!s_fp_page_lbl) return;
    const uint16_t page_size = 8u;
    uint16_t pages = (s_fp_present_count + page_size - 1u) / page_size;
    if (pages == 0) pages = 1;
    if (s_fp_page >= pages) s_fp_page = pages - 1u;
    char buf[32];
    snprintf(buf, sizeof(buf), "Page %u/%u", (unsigned)(s_fp_page + 1u), (unsigned)pages);
    lv_label_set_text(s_fp_page_lbl, buf);
}

static void fp_update_sel_label(void)
{
    if (!s_fp_sel_lbl) return;
    char buf[48];
    snprintf(buf, sizeof(buf), "Selected: %u", (unsigned)fp_selected_count());
    lv_label_set_text(s_fp_sel_lbl, buf);
}

static void fp_list_item_set_style(lv_obj_t *btn, bool selected)
{
    if (!btn) return;
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x24304A), 0);
    if (selected) {
        lv_obj_set_style_bg_color(btn, c_accent(), 0);
    } else {
        lv_obj_set_style_bg_color(btn, c_card2(), 0);
    }
}

static void fp_manage_rebuild_list_ui(void)
{
    if (!s_fp_list) return;
    lv_obj_clean(s_fp_list);

    const uint16_t page_size = 8u;
    fp_update_page_label();

    uint16_t start = (uint16_t)(s_fp_page * page_size);
    uint16_t end = start + page_size;
    if (end > s_fp_present_count) end = s_fp_present_count;

    for (uint16_t i = start; i < end; i++) {
        uint16_t id = s_fp_present_ids[i];
        bool selected = bit_get_u8(s_fp_selected_bits, id);

        lv_obj_t *btn = lv_btn_create(s_fp_list);
        lv_obj_set_size(btn, 340, 56);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        fp_list_item_set_style(btn, selected);

        lv_obj_t *lbl = lv_label_create(btn);
        char text[32];
        snprintf(text, sizeof(text), "%s  ID %u", selected ? LV_SYMBOL_OK : "  ", (unsigned)id);
        lv_label_set_text(lbl, text);
        lv_obj_set_style_text_color(lbl, selected ? lv_color_white() : c_text(), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 14, 0);

        /* 通过指针强转把 ID 存进 event user_data（轻量传参） */
        lv_obj_add_event_cb(btn, fp_item_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)id);
        lv_obj_add_event_cb(btn, fp_item_long_cb, LV_EVENT_LONG_PRESSED, (void *)(uintptr_t)id);
    }

    fp_update_sel_label();
}

static void fp_item_click_cb(lv_event_t *e)
{
    uint16_t id = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
    bool now_selected = !bit_get_u8(s_fp_selected_bits, id);
    bit_set_u8(s_fp_selected_bits, id, now_selected);

    lv_obj_t *btn = lv_event_get_target(e);
    fp_list_item_set_style(btn, now_selected);

    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    if (lbl) {
        char text[32];
        snprintf(text, sizeof(text), "%s  ID %u", now_selected ? LV_SYMBOL_OK : "  ", (unsigned)id);
        lv_label_set_text(lbl, text);
        lv_obj_set_style_text_color(lbl, now_selected ? lv_color_white() : c_text(), 0);
    }

    fp_update_sel_label();
}

static void fp_item_long_cb(lv_event_t *e)
{
    uint16_t id = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
    s_fp_target_id = id;
    fp_update_id_label();
    ui_toast("ID selected", c_text(), 800);
}

static void fp_apply_cb(void *user_data);

static void fp_worker(void *arg)
{
    (void)arg;
    for (;;) {
        fp_cmd_t cmd;
        if (xQueueReceive(s_fp_q, &cmd, portMAX_DELAY) != pdTRUE) continue;

        fp_res_t *res = (fp_res_t *)pvPortMalloc(sizeof(fp_res_t));
        if (!res) {
            s_fp_busy = false;
            continue;
        }
        memset(res, 0, sizeof(*res));
        res->op = cmd.op;
        res->id = cmd.id;
        res->st = AS608_STATUS_UNKNOWN;

        switch (cmd.op) {
            case FP_OP_VERIFY:
                res->rc = AS608_CRUD_Read(8000, &res->found_id, &res->score, &res->st);
                break;
            case FP_OP_ENROLL:
                res->rc = AS608_CRUD_Create(cmd.id, 15000, &res->st);
                break;
            case FP_OP_DELETE:
                res->rc = AS608_CRUD_Delete(cmd.id, &res->st);
                break;
            case FP_OP_CLEAR:
                res->rc = AS608_CRUD_ClearAll(&res->st);
                break;
            case FP_OP_LIST_INDEX: {
                res->capacity = AS608_Get_Capacity();
                if (res->capacity == 0) res->capacity = 300;

                memset(res->index_bits, 0, sizeof(res->index_bits));
                res->rc = AS608_SVC_OK;
                res->st = AS608_STATUS_OK;

                for (uint8_t num = 0; num < 4u; num++) {
                    if ((uint32_t)num * 256u >= res->capacity) break;
                    uint8_t table[32];
                    memset(table, 0, sizeof(table));
                    as608_status_t st = AS608_STATUS_UNKNOWN;
                    as608_svc_rc_t rc = AS608_List_IndexTable(num, table, &st);
                    if (rc != AS608_SVC_OK || st != AS608_STATUS_OK) {
                        res->rc = rc;
                        res->st = st;
                        break;
                    }
                    memcpy(&res->index_bits[num * 32u], table, 32u);
                }
                break;
            }
            default:
                res->rc = AS608_SVC_ERR;
                break;
        }

        lvgl_lock();
        if (lv_async_call(fp_apply_cb, res) != LV_RES_OK) {
            lvgl_unlock();
            vPortFree(res);
            s_fp_busy = false;
            continue;
        }
        lvgl_unlock();
    }
}

static bool fp_submit(fp_cmd_t cmd)
{
    if (!s_fp_q) return false;
    if (s_fp_busy) return false;
    s_fp_busy = true;
    return xQueueSend(s_fp_q, &cmd, 0) == pdTRUE;
}

static void fp_back_to_choose(lv_event_t *e) { (void)e; nav_to(s_choose); }
static void fp_back_to_unlock(lv_event_t *e) { (void)e; nav_to(s_fp_unlock); }

static bool fp_id_valid(uint16_t id)
{
    if (s_fp_capacity == 0) return (id < 1024u);
    return (id < s_fp_capacity);
}

static void fp_request_index_refresh(void)
{
    set_label(s_fp_manage_status, "Refreshing list...", c_warn());
    if (!fp_submit((fp_cmd_t){.op = FP_OP_LIST_INDEX, .id = 0})) {
        set_label(s_fp_manage_status, "Busy...", c_bad());
    }
}

static void fp_open_manage_cb(lv_event_t *e)
{
    (void)e;
    nav_to(s_fp_manage);
    fp_request_index_refresh();
}

static void fp_refresh_cb(lv_event_t *e)
{
    (void)e;
    fp_request_index_refresh();
}

static void fp_unlock_start_cb(lv_event_t *e)
{
    (void)e;
    set_label(s_fp_unlock_status, "Place finger on sensor...", c_warn());
    if (!fp_submit((fp_cmd_t){.op = FP_OP_VERIFY, .id = 0})) {
        set_label(s_fp_unlock_status, "Busy...", c_bad());
    }
}

static void fp_id_ok_cb(const char *text, void *user_ctx)
{
    (void)user_ctx;
    uint16_t id = (uint16_t)atoi(text ? text : "0");
    if (!fp_id_valid(id)) {
        set_label(s_fp_manage_status, "Invalid ID", c_bad());
        return;
    }
    s_fp_target_id = id;
    fp_update_id_label();
    set_label(s_fp_manage_status, "Ready", c_sub());
}

static void fp_edit_id_cb(lv_event_t *e)
{
    (void)e;
    char init[16];
    snprintf(init, sizeof(init), "%u", (unsigned)s_fp_target_id);
    ui_popup_keypad_num("Fingerprint ID", init, 4, fp_id_ok_cb, NULL);
}

static void fp_enroll_cb(lv_event_t *e)
{
    (void)e;
    uint16_t id = s_fp_target_id;
    if (!fp_id_valid(id)) {
        set_label(s_fp_manage_status, "Invalid ID", c_bad());
        return;
    }
    set_label(s_fp_manage_status, "Enrolling (press finger twice)...", c_warn());
    if (!fp_submit((fp_cmd_t){.op = FP_OP_ENROLL, .id = id})) {
        set_label(s_fp_manage_status, "Busy...", c_bad());
    }
}

static void fp_clear_cb(lv_event_t *e)
{
    (void)e;
    memset(s_fp_selected_bits, 0, sizeof(s_fp_selected_bits));
    set_label(s_fp_manage_status, "Clearing all templates...", c_warn());
    if (!fp_submit((fp_cmd_t){.op = FP_OP_CLEAR, .id = 0})) {
        set_label(s_fp_manage_status, "Busy...", c_bad());
    }
}

static void fp_prev_page_cb(lv_event_t *e)
{
    (void)e;
    if (s_fp_page > 0) s_fp_page--;
    fp_manage_rebuild_list_ui();
}

static void fp_next_page_cb(lv_event_t *e)
{
    (void)e;
    const uint16_t page_size = 8u;
    uint16_t pages = (s_fp_present_count + page_size - 1u) / page_size;
    if (pages == 0) pages = 1;
    if ((s_fp_page + 1u) < pages) s_fp_page++;
    fp_manage_rebuild_list_ui();
}

static void fp_delete_selected_start(void)
{
    s_fp_del_batch_active = false;
    s_fp_del_batch_count = 0;
    s_fp_del_batch_pos = 0;
    s_fp_del_batch_ok = 0;
    s_fp_del_batch_fail = 0;

    for (uint16_t i = 0; i < s_fp_present_count; i++) {
        uint16_t id = s_fp_present_ids[i];
        if (!bit_get_u8(s_fp_selected_bits, id)) continue;
        if (s_fp_del_batch_count < (uint8_t)(sizeof(s_fp_del_batch_ids) / sizeof(s_fp_del_batch_ids[0]))) {
            s_fp_del_batch_ids[s_fp_del_batch_count++] = id;
        }
    }

    if (s_fp_del_batch_count == 0) {
        ui_toast("No ID selected", c_text(), 900);
        return;
    }

    s_fp_del_batch_active = true;
    char buf[64];
    snprintf(buf, sizeof(buf), "Deleting %u templates...", (unsigned)s_fp_del_batch_count);
    set_label(s_fp_manage_status, buf, c_warn());

    if (!fp_submit((fp_cmd_t){.op = FP_OP_DELETE, .id = s_fp_del_batch_ids[0]})) {
        set_label(s_fp_manage_status, "Busy...", c_bad());
        s_fp_del_batch_active = false;
    }
}

static void fp_delete_selected_cb(lv_event_t *e)
{
    (void)e;
    fp_delete_selected_start();
}

static void fp_apply_cb(void *user_data)
{
    fp_res_t *res = (fp_res_t *)user_data;
    if (!res) {
        s_fp_busy = false;
        return;
    }

    char buf[96];
    bool submit_next_delete = false;
    bool request_refresh = false;

    switch (res->op) {
        case FP_OP_VERIFY:
            if (res->rc == AS608_SVC_OK && res->st == AS608_STATUS_OK) {
                snprintf(buf, sizeof(buf), "Match: ID %u  (score %u)", (unsigned)res->found_id, (unsigned)res->score);
                set_label(s_fp_unlock_status, buf, c_good());
                ui_modal_result("Unlocked", "Fingerprint verified", true, 1500);
                nav_to(s_home);
            } else {
                snprintf(buf, sizeof(buf), "Verify fail: rc=%s st=%s", fp_rc_str(res->rc), fp_st_str(res->st));
                set_label(s_fp_unlock_status, buf, c_bad());
                ui_modal_result("Access denied", "Fingerprint not recognized", false, 1500);
            }
            break;
        case FP_OP_ENROLL:
            if (res->rc == AS608_SVC_OK && res->st == AS608_STATUS_OK) {
                snprintf(buf, sizeof(buf), "Enrolled: ID %u", (unsigned)res->id);
                set_label(s_fp_manage_status, buf, c_good());
                ui_toast("Fingerprint enrolled", c_good(), 1200);
                request_refresh = true;
            } else {
                snprintf(buf, sizeof(buf), "Enroll fail: rc=%s st=%s", fp_rc_str(res->rc), fp_st_str(res->st));
                set_label(s_fp_manage_status, buf, c_bad());
            }
            break;
        case FP_OP_DELETE:
            if (s_fp_del_batch_active) {
                if (res->rc == AS608_SVC_OK && res->st == AS608_STATUS_OK) s_fp_del_batch_ok++;
                else s_fp_del_batch_fail++;

                s_fp_del_batch_pos++;
                if (s_fp_del_batch_pos >= s_fp_del_batch_count) {
                    s_fp_del_batch_active = false;
                    memset(s_fp_selected_bits, 0, sizeof(s_fp_selected_bits));
                    snprintf(buf, sizeof(buf), "Deleted %u, failed %u",
                             (unsigned)s_fp_del_batch_ok, (unsigned)s_fp_del_batch_fail);
                    ui_modal_result("Delete finished", buf, (s_fp_del_batch_fail == 0), 1700);
                    request_refresh = true;
                } else {
                    submit_next_delete = true;
                }
            } else {
                if (res->rc == AS608_SVC_OK && res->st == AS608_STATUS_OK) {
                    snprintf(buf, sizeof(buf), "Deleted: ID %u", (unsigned)res->id);
                    set_label(s_fp_manage_status, buf, c_good());
                    request_refresh = true;
                } else {
                    snprintf(buf, sizeof(buf), "Delete fail: rc=%s st=%s", fp_rc_str(res->rc), fp_st_str(res->st));
                    set_label(s_fp_manage_status, buf, c_bad());
                }
            }
            break;
        case FP_OP_CLEAR:
            if (res->rc == AS608_SVC_OK && res->st == AS608_STATUS_OK) {
                set_label(s_fp_manage_status, "Cleared all fingerprints", c_good());
                request_refresh = true;
            } else {
                snprintf(buf, sizeof(buf), "Clear fail: rc=%s st=%s", fp_rc_str(res->rc), fp_st_str(res->st));
                set_label(s_fp_manage_status, buf, c_bad());
            }
            break;
        case FP_OP_LIST_INDEX:
            if (res->rc == AS608_SVC_OK && res->st == AS608_STATUS_OK) {
                s_fp_capacity = res->capacity;
                memcpy(s_fp_index_bits, res->index_bits, sizeof(s_fp_index_bits));
                fp_rebuild_present_ids();
                fp_update_id_label();
                fp_manage_rebuild_list_ui();
                fp_update_page_label();
                fp_update_sel_label();
                snprintf(buf, sizeof(buf), "Loaded %u template(s)", (unsigned)s_fp_present_count);
                set_label(s_fp_manage_status, buf, c_good());
            } else {
                snprintf(buf, sizeof(buf), "List fail: rc=%s st=%s", fp_rc_str(res->rc), fp_st_str(res->st));
                set_label(s_fp_manage_status, buf, c_bad());
            }
            break;
        default:
            break;
    }

    s_fp_busy = false;
    vPortFree(res);

    if (submit_next_delete && s_fp_del_batch_active && (s_fp_del_batch_pos < s_fp_del_batch_count)) {
        uint16_t next_id = s_fp_del_batch_ids[s_fp_del_batch_pos];
        snprintf(buf, sizeof(buf), "Deleting ID %u (%u/%u)...",
                 (unsigned)next_id,
                 (unsigned)(s_fp_del_batch_pos + 1u),
                 (unsigned)s_fp_del_batch_count);
        set_label(s_fp_manage_status, buf, c_warn());
        if (!fp_submit((fp_cmd_t){.op = FP_OP_DELETE, .id = next_id})) {
            set_label(s_fp_manage_status, "Busy...", c_bad());
            s_fp_del_batch_active = false;
        }
    } else if (request_refresh) {
        fp_request_index_refresh();
    }
}

static void build_fp_unlock(void)
{
    s_fp_unlock = lv_obj_create(NULL);
    style_screen(s_fp_unlock);
    ui_make_topbar(s_fp_unlock, "Fingerprint", fp_back_to_choose);

    lv_obj_t *card = lv_obj_create(s_fp_unlock);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(card, 760, 300);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_radius(card, 24, 0);
    lv_obj_set_style_bg_color(card, c_card(), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x233152), 0);
    lv_obj_set_style_pad_all(card, 22, 0);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "Scan your fingerprint");
    lv_obj_set_style_text_color(title, c_text(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *sub = lv_label_create(card);
    lv_label_set_text(sub, "Tip: use Manage to enroll/delete/clear.");
    lv_obj_set_style_text_color(sub, c_sub(), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 0, 34);

    lv_obj_t *btn = lv_btn_create(card);
    lv_obj_set_size(btn, 260, 56);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_radius(btn, 18, 0);
    lv_obj_set_style_bg_color(btn, c_accent(), 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, fp_unlock_start_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, LV_SYMBOL_OK "  Verify & Unlock");
    lv_obj_set_style_text_color(bl, lv_color_white(), 0);
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_16, 0);
    lv_obj_center(bl);

    lv_obj_t *btn2 = lv_btn_create(card);
    lv_obj_set_size(btn2, 180, 56);
    lv_obj_align(btn2, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_radius(btn2, 18, 0);
    lv_obj_set_style_bg_color(btn2, c_card2(), 0);
    lv_obj_set_style_border_width(btn2, 1, 0);
    lv_obj_set_style_border_color(btn2, lv_color_hex(0x233152), 0);
    lv_obj_add_event_cb(btn2, fp_open_manage_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *bl2 = lv_label_create(btn2);
    lv_label_set_text(bl2, LV_SYMBOL_SETTINGS "  Manage");
    lv_obj_set_style_text_color(bl2, c_text(), 0);
    lv_obj_set_style_text_font(bl2, &lv_font_montserrat_16, 0);
    lv_obj_center(bl2);

    s_fp_unlock_status = lv_label_create(card);
    lv_label_set_text(s_fp_unlock_status, "Ready");
    lv_obj_set_style_text_color(s_fp_unlock_status, c_sub(), 0);
    lv_obj_set_style_text_font(s_fp_unlock_status, &lv_font_montserrat_14, 0);
    lv_obj_align(s_fp_unlock_status, LV_ALIGN_BOTTOM_MID, 0, -76);
}

static void build_fp_manage(void)
{
    s_fp_manage = lv_obj_create(NULL);
    style_screen(s_fp_manage);
    ui_make_topbar(s_fp_manage, "Fingerprint · Manage", fp_back_to_unlock);

    lv_obj_t *card = lv_obj_create(s_fp_manage);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(card, 760, 360);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 12);
    lv_obj_set_style_radius(card, 24, 0);
    lv_obj_set_style_bg_color(card, c_card(), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x24304A), 0);
    lv_obj_set_style_pad_all(card, 22, 0);
    lv_obj_set_style_pad_gap(card, 12, 0);

    /* ID 输入框（点击编辑） */
    s_fp_id_btn = lv_btn_create(card);
    lv_obj_set_size(s_fp_id_btn, 340, 54);
    lv_obj_align(s_fp_id_btn, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_radius(s_fp_id_btn, 18, 0);
    lv_obj_set_style_bg_color(s_fp_id_btn, c_card2(), 0);
    lv_obj_set_style_border_width(s_fp_id_btn, 1, 0);
    lv_obj_set_style_border_color(s_fp_id_btn, lv_color_hex(0x24304A), 0);
    lv_obj_add_event_cb(s_fp_id_btn, fp_edit_id_cb, LV_EVENT_CLICKED, NULL);

    s_fp_id_lbl = lv_label_create(s_fp_id_btn);
    lv_obj_set_style_text_color(s_fp_id_lbl, c_text(), 0);
    lv_obj_set_style_text_font(s_fp_id_lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(s_fp_id_lbl, LV_ALIGN_LEFT_MID, 14, 0);
    fp_update_id_label();

    lv_obj_t *btn_en = lv_btn_create(card);
    lv_obj_set_size(btn_en, 190, 54);
    lv_obj_align(btn_en, LV_ALIGN_TOP_LEFT, 360, 0);
    lv_obj_set_style_radius(btn_en, 18, 0);
    lv_obj_set_style_bg_color(btn_en, c_accent(), 0);
    lv_obj_set_style_border_width(btn_en, 0, 0);
    lv_obj_add_event_cb(btn_en, fp_enroll_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *t1 = lv_label_create(btn_en);
    lv_label_set_text(t1, LV_SYMBOL_PLUS "  Enroll");
    lv_obj_set_style_text_color(t1, lv_color_white(), 0);
    lv_obj_set_style_text_font(t1, &lv_font_montserrat_16, 0);
    lv_obj_center(t1);

    lv_obj_t *btn_refresh = lv_btn_create(card);
    lv_obj_set_size(btn_refresh, 190, 54);
    lv_obj_align(btn_refresh, LV_ALIGN_TOP_LEFT, 570, 0);
    lv_obj_set_style_radius(btn_refresh, 18, 0);
    lv_obj_set_style_bg_color(btn_refresh, c_card2(), 0);
    lv_obj_set_style_border_width(btn_refresh, 1, 0);
    lv_obj_set_style_border_color(btn_refresh, lv_color_hex(0x24304A), 0);
    lv_obj_add_event_cb(btn_refresh, fp_refresh_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *tr = lv_label_create(btn_refresh);
    lv_label_set_text(tr, LV_SYMBOL_REFRESH "  Refresh");
    lv_obj_set_style_text_color(tr, c_text(), 0);
    lv_obj_set_style_text_font(tr, &lv_font_montserrat_16, 0);
    lv_obj_center(tr);

    /* 已存在的 ID 列表（分页） */
    s_fp_list = lv_obj_create(card);
    lv_obj_clear_flag(s_fp_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_fp_list, 716, 188);
    lv_obj_align(s_fp_list, LV_ALIGN_TOP_LEFT, 0, 72);
    lv_obj_set_style_bg_opa(s_fp_list, LV_OPA_0, 0);
    lv_obj_set_style_border_width(s_fp_list, 0, 0);
    lv_obj_set_style_pad_all(s_fp_list, 0, 0);
    lv_obj_set_style_pad_gap(s_fp_list, 12, 0);
    lv_obj_set_flex_flow(s_fp_list, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_fp_list, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *page_bar = lv_obj_create(card);
    lv_obj_clear_flag(page_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(page_bar, 716, 46);
    lv_obj_align(page_bar, LV_ALIGN_TOP_LEFT, 0, 268);
    lv_obj_set_style_bg_opa(page_bar, LV_OPA_0, 0);
    lv_obj_set_style_border_width(page_bar, 0, 0);
    lv_obj_set_style_pad_all(page_bar, 0, 0);

    lv_obj_t *btn_prev = lv_btn_create(page_bar);
    lv_obj_set_size(btn_prev, 120, 42);
    lv_obj_align(btn_prev, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(btn_prev, 16, 0);
    lv_obj_set_style_bg_color(btn_prev, c_card2(), 0);
    lv_obj_set_style_border_width(btn_prev, 1, 0);
    lv_obj_set_style_border_color(btn_prev, lv_color_hex(0x24304A), 0);
    lv_obj_add_event_cb(btn_prev, fp_prev_page_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lp = lv_label_create(btn_prev);
    lv_label_set_text(lp, LV_SYMBOL_LEFT "  Prev");
    lv_obj_set_style_text_color(lp, c_text(), 0);
    lv_obj_center(lp);

    s_fp_page_lbl = lv_label_create(page_bar);
    lv_label_set_text(s_fp_page_lbl, "Page 1/1");
    lv_obj_set_style_text_color(s_fp_page_lbl, c_sub(), 0);
    lv_obj_set_style_text_font(s_fp_page_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(s_fp_page_lbl, LV_ALIGN_CENTER, 0, 0);

    s_fp_sel_lbl = lv_label_create(page_bar);
    lv_label_set_text(s_fp_sel_lbl, "Selected: 0");
    lv_obj_set_style_text_color(s_fp_sel_lbl, c_sub(), 0);
    lv_obj_set_style_text_font(s_fp_sel_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(s_fp_sel_lbl, LV_ALIGN_RIGHT_MID, -140, 0);

    lv_obj_t *btn_next = lv_btn_create(page_bar);
    lv_obj_set_size(btn_next, 120, 42);
    lv_obj_align(btn_next, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(btn_next, 16, 0);
    lv_obj_set_style_bg_color(btn_next, c_card2(), 0);
    lv_obj_set_style_border_width(btn_next, 1, 0);
    lv_obj_set_style_border_color(btn_next, lv_color_hex(0x24304A), 0);
    lv_obj_add_event_cb(btn_next, fp_next_page_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ln = lv_label_create(btn_next);
    lv_label_set_text(ln, "Next  " LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(ln, c_text(), 0);
    lv_obj_center(ln);

    /* 操作区 */
    lv_obj_t *btn_del_sel = lv_btn_create(card);
    lv_obj_set_size(btn_del_sel, 260, 56);
    lv_obj_align(btn_del_sel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_radius(btn_del_sel, 18, 0);
    lv_obj_set_style_bg_color(btn_del_sel, lv_color_hex(0x24304A), 0);
    lv_obj_set_style_border_width(btn_del_sel, 1, 0);
    lv_obj_set_style_border_color(btn_del_sel, lv_color_hex(0x24304A), 0);
    lv_obj_add_event_cb(btn_del_sel, fp_delete_selected_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *td = lv_label_create(btn_del_sel);
    lv_label_set_text(td, LV_SYMBOL_TRASH "  Delete Selected");
    lv_obj_set_style_text_color(td, c_text(), 0);
    lv_obj_set_style_text_font(td, &lv_font_montserrat_16, 0);
    lv_obj_center(td);

    lv_obj_t *btn_clr = lv_btn_create(card);
    lv_obj_set_size(btn_clr, 190, 56);
    lv_obj_align(btn_clr, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_radius(btn_clr, 18, 0);
    lv_obj_set_style_bg_color(btn_clr, c_card2(), 0);
    lv_obj_set_style_border_width(btn_clr, 1, 0);
    lv_obj_set_style_border_color(btn_clr, lv_color_hex(0x24304A), 0);
    lv_obj_add_event_cb(btn_clr, fp_clear_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *tc = lv_label_create(btn_clr);
    lv_label_set_text(tc, LV_SYMBOL_CLOSE "  Clear All");
    lv_obj_set_style_text_color(tc, c_text(), 0);
    lv_obj_set_style_text_font(tc, &lv_font_montserrat_16, 0);
    lv_obj_center(tc);

    s_fp_manage_status = lv_label_create(card);
    lv_label_set_text(s_fp_manage_status, "Tap Refresh to load IDs");
    lv_obj_set_style_text_color(s_fp_manage_status, c_sub(), 0);
    lv_obj_set_style_text_font(s_fp_manage_status, &lv_font_montserrat_14, 0);
    lv_obj_align(s_fp_manage_status, LV_ALIGN_BOTTOM_LEFT, 0, -64);
}

/* ================= RFID（MFRC522） ================= */

typedef enum {
    RFID_OP_VERIFY = 0,
    RFID_OP_ENROLL,
    RFID_OP_DELETE,
    RFID_OP_CLEAR,
} rfid_op_t;

typedef struct {
    rfid_op_t op;
} rfid_cmd_t;

typedef struct {
    rfid_op_t op;
    bool ok;
    uint8_t uid[4];
    char msg[80];
} rfid_res_t;

static QueueHandle_t s_rfid_q = NULL;
static TaskHandle_t s_rfid_task = NULL;
static volatile bool s_rfid_busy = false;

static lv_obj_t *s_rfid_unlock_status = NULL;
static lv_obj_t *s_rfid_manage_status = NULL;
static lv_obj_t *s_rfid_last_uid = NULL;

/* RFID 管理页 UI 状态 */
static lv_obj_t *s_rfid_list = NULL;
static lv_obj_t *s_rfid_page_lbl = NULL;
static lv_obj_t *s_rfid_sel_lbl = NULL;
static uint16_t s_rfid_page = 0;
static uint32_t s_rfid_sel_mask = 0; /* selection by current list index (rebuilt on refresh) */

static uint16_t rfid_sel_count(uint8_t count)
{
    uint16_t selected = 0;
    for (uint8_t i = 0; i < count && i < 32u; i++) {
        if (s_rfid_sel_mask & (1u << i)) selected++;
    }
    return selected;
}

static void rfid_update_page_label(uint8_t total)
{
    if (!s_rfid_page_lbl) return;
    const uint16_t page_size = 8u;
    uint16_t pages = ((uint16_t)total + page_size - 1u) / page_size;
    if (pages == 0) pages = 1;
    if (s_rfid_page >= pages) s_rfid_page = pages - 1u;
    char buf[24];
    snprintf(buf, sizeof(buf), "Page %u/%u", (unsigned)(s_rfid_page + 1u), (unsigned)pages);
    lv_label_set_text(s_rfid_page_lbl, buf);
}

static void rfid_update_sel_label(uint8_t total)
{
    if (!s_rfid_sel_lbl) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "Selected: %u", (unsigned)rfid_sel_count(total));
    lv_label_set_text(s_rfid_sel_lbl, buf);
}

static void rfid_item_set_style(lv_obj_t *btn, bool selected)
{
    if (!btn) return;
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x24304A), 0);
    lv_obj_set_style_bg_color(btn, selected ? c_accent() : c_card2(), 0);
}

static void rfid_item_click_cb(lv_event_t *e)
{
    uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    uint8_t total = lock_rfid_count();
    if (idx >= total || idx >= 32u) return;

    s_rfid_sel_mask ^= (1u << idx);
    bool selected = (s_rfid_sel_mask & (1u << idx)) != 0u;

    lv_obj_t *btn = lv_event_get_target(e);
    rfid_item_set_style(btn, selected);

    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    if (lbl) {
        lock_rfid_entry_t entry;
        if (lock_rfid_get(idx, &entry)) {
            char line[48];
            snprintf(line, sizeof(line), "%s  UID %02X%02X%02X%02X",
                     selected ? LV_SYMBOL_OK : "  ",
                     entry.uid[0], entry.uid[1], entry.uid[2], entry.uid[3]);
            lv_label_set_text(lbl, line);
            lv_obj_set_style_text_color(lbl, selected ? lv_color_white() : c_text(), 0);
        }
    }

    rfid_update_sel_label(total);
}

static void rfid_manage_rebuild_list_ui(void)
{
    if (!s_rfid_list) return;
    lv_obj_clean(s_rfid_list);

    uint8_t total = lock_rfid_count();
    const uint16_t page_size = 8u;
    rfid_update_page_label(total);

    uint16_t start = (uint16_t)(s_rfid_page * page_size);
    uint16_t end = start + page_size;
    if (end > total) end = total;

    for (uint16_t i = start; i < end; i++) {
        uint8_t idx = (uint8_t)i;
        lock_rfid_entry_t entry;
        if (!lock_rfid_get(idx, &entry)) continue;

        bool selected = (idx < 32u) && ((s_rfid_sel_mask & (1u << idx)) != 0u);

        lv_obj_t *btn = lv_btn_create(s_rfid_list);
        lv_obj_set_size(btn, 340, 56);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        rfid_item_set_style(btn, selected);
        lv_obj_add_event_cb(btn, rfid_item_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)idx);

        lv_obj_t *lbl = lv_label_create(btn);
        char line[48];
        snprintf(line, sizeof(line), "%s  UID %02X%02X%02X%02X",
                 selected ? LV_SYMBOL_OK : "  ",
                 entry.uid[0], entry.uid[1], entry.uid[2], entry.uid[3]);
        lv_label_set_text(lbl, line);
        lv_obj_set_style_text_color(lbl, selected ? lv_color_white() : c_text(), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 14, 0);
    }

    rfid_update_sel_label(total);
}

static bool rfid_wait_uid(uint32_t timeout_ms, uint8_t out_uid[4])
{
    TickType_t start = xTaskGetTickCount();
    unsigned char tag_type[2] = {0};
    unsigned char uid6[6] = {0};

    while ((uint32_t)((xTaskGetTickCount() - start) * portTICK_PERIOD_MS) < timeout_ms) {
        char st = PcdRequest(PICC_REQALL, tag_type);
        if (st == MI_OK) {
            st = PcdAnticoll(uid6);
            if (st == MI_OK) {
                (void)PcdSelect(uid6);
                out_uid[0] = uid6[0];
                out_uid[1] = uid6[1];
                out_uid[2] = uid6[2];
                out_uid[3] = uid6[3];
                PcdHalt();
                return true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(80));
    }
    return false;
}

static void rfid_apply_cb(void *user_data);

static void rfid_worker(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(300));
    RC522_Init();
    LOG_I("UI_RFID", "RC522 VersionReg=0x%02X", (unsigned)ReadRawRC(VersionReg));

    for (;;) {
        rfid_cmd_t cmd;
        if (xQueueReceive(s_rfid_q, &cmd, portMAX_DELAY) != pdTRUE) continue;

        rfid_res_t *res = (rfid_res_t *)pvPortMalloc(sizeof(rfid_res_t));
        if (!res) {
            s_rfid_busy = false;
            continue;
        }
        memset(res, 0, sizeof(*res));
        res->op = cmd.op;

        if (cmd.op == RFID_OP_CLEAR) {
            lock_rfid_clear();
            res->ok = true;
            snprintf(res->msg, sizeof(res->msg), "All cards cleared");
        } else {
            uint8_t uid[4] = {0};
            bool got = rfid_wait_uid(8000, uid);
            if (!got) {
                res->ok = false;
                snprintf(res->msg, sizeof(res->msg), "Timeout: no card detected");
            } else {
                memcpy(res->uid, uid, 4);
                if (cmd.op == RFID_OP_VERIFY) {
                    if (lock_rfid_count() == 0) {
                        res->ok = false;
                        snprintf(res->msg, sizeof(res->msg), "No cards enrolled");
                    } else if (lock_rfid_find_uid(uid) >= 0) {
                        res->ok = true;
                        snprintf(res->msg, sizeof(res->msg), "Card accepted");
                    } else {
                        res->ok = false;
                        snprintf(res->msg, sizeof(res->msg), "Card not authorized");
                    }
                } else if (cmd.op == RFID_OP_ENROLL) {
                    res->ok = lock_rfid_add_uid(uid, NULL);
                    snprintf(res->msg, sizeof(res->msg), res->ok ? "Card enrolled" : "Enroll failed (DB full)");
                } else if (cmd.op == RFID_OP_DELETE) {
                    res->ok = lock_rfid_remove_uid(uid);
                    snprintf(res->msg, sizeof(res->msg), res->ok ? "Card deleted" : "Card not found");
                }
            }
        }

        lvgl_lock();
        if (lv_async_call(rfid_apply_cb, res) != LV_RES_OK) {
            lvgl_unlock();
            vPortFree(res);
            s_rfid_busy = false;
            continue;
        }
        lvgl_unlock();
    }
}

static bool rfid_submit(rfid_cmd_t cmd)
{
    if (!s_rfid_q) return false;
    if (s_rfid_busy) return false;
    s_rfid_busy = true;
    return xQueueSend(s_rfid_q, &cmd, 0) == pdTRUE;
}

static void rfid_back_to_choose(lv_event_t *e) { (void)e; nav_to(s_choose); }
static void rfid_back_to_unlock(lv_event_t *e) { (void)e; nav_to(s_rfid_unlock); }

static void rfid_open_manage_cb(lv_event_t *e)
{
    (void)e;
    s_rfid_sel_mask = 0;
    s_rfid_page = 0;
    nav_to(s_rfid_manage);
    rfid_manage_rebuild_list_ui();
    set_label(s_rfid_manage_status, "Ready", c_sub());
}

static void rfid_manage_refresh_cb(lv_event_t *e)
{
    (void)e;
    s_rfid_sel_mask = 0;
    s_rfid_page = 0;
    rfid_manage_rebuild_list_ui();
    set_label(s_rfid_manage_status, "List refreshed", c_good());
}

static void rfid_prev_page_cb(lv_event_t *e)
{
    (void)e;
    if (s_rfid_page > 0) s_rfid_page--;
    rfid_manage_rebuild_list_ui();
}

static void rfid_next_page_cb(lv_event_t *e)
{
    (void)e;
    const uint16_t page_size = 8u;
    uint8_t total = lock_rfid_count();
    uint16_t pages = ((uint16_t)total + page_size - 1u) / page_size;
    if (pages == 0) pages = 1;
    if ((s_rfid_page + 1u) < pages) s_rfid_page++;
    rfid_manage_rebuild_list_ui();
}

static void rfid_verify_cb(lv_event_t *e)
{
    (void)e;
    set_label(s_rfid_unlock_status, "Tap card to unlock...", c_warn());
    if (!rfid_submit((rfid_cmd_t){.op = RFID_OP_VERIFY})) {
        set_label(s_rfid_unlock_status, "Busy...", c_bad());
    }
}

static void rfid_scan_enroll_cb(lv_event_t *e)
{
    (void)e;
    set_label(s_rfid_manage_status, "Tap card to enroll...", c_warn());
    if (!rfid_submit((rfid_cmd_t){.op = RFID_OP_ENROLL})) {
        set_label(s_rfid_manage_status, "Busy...", c_bad());
    }
}

static void rfid_delete_selected_cb(lv_event_t *e)
{
    (void)e;
    uint8_t total = lock_rfid_count();
    if (total == 0) {
        ui_toast("No cards enrolled", c_text(), 900);
        return;
    }

    uint8_t removed = 0;
    for (uint8_t i = 0; i < total && i < 32u; i++) {
        if ((s_rfid_sel_mask & (1u << i)) == 0u) continue;
        lock_rfid_entry_t entry;
        if (lock_rfid_get(i, &entry)) {
            if (lock_rfid_remove_uid(entry.uid)) removed++;
        }
    }

    s_rfid_sel_mask = 0;
    rfid_manage_rebuild_list_ui();

    char msg[48];
    snprintf(msg, sizeof(msg), "Removed %u card(s)", (unsigned)removed);
    ui_modal_result("RFID updated", msg, true, 1500);
    set_label(s_rfid_manage_status, msg, c_good());
}

static void rfid_clear_all_cb(lv_event_t *e)
{
    (void)e;
    lock_rfid_clear();
    s_rfid_sel_mask = 0;
    s_rfid_page = 0;
    rfid_manage_rebuild_list_ui();
    ui_modal_result("RFID cleared", "All cards removed", true, 1500);
    set_label(s_rfid_manage_status, "All cards cleared", c_good());
}

static void rfid_apply_cb(void *user_data)
{
    rfid_res_t *res = (rfid_res_t *)user_data;
    if (!res) {
        s_rfid_busy = false;
        return;
    }

    if (res->uid[0] || res->uid[1] || res->uid[2] || res->uid[3]) {
        char uidbuf[32];
        snprintf(uidbuf, sizeof(uidbuf), "UID %02X%02X%02X%02X", res->uid[0], res->uid[1], res->uid[2], res->uid[3]);
        if (s_rfid_last_uid) lv_label_set_text(s_rfid_last_uid, uidbuf);
    }

    lv_color_t col = res->ok ? c_good() : c_bad();
    if (res->op == RFID_OP_VERIFY) {
        set_label(s_rfid_unlock_status, res->msg, col);
        if (res->ok) {
            ui_modal_result("Unlocked", "RFID verified", true, 1500);
            nav_to(s_home);
        } else {
            ui_modal_result("Access denied", "RFID not authorized", false, 1500);
        }
    } else {
        set_label(s_rfid_manage_status, res->msg, col);
        /* 让管理列表与存储内容保持同步 */
        rfid_manage_rebuild_list_ui();
    }

    s_rfid_busy = false;
    vPortFree(res);
}

static void build_rfid_unlock(void)
{
    s_rfid_unlock = lv_obj_create(NULL);
    style_screen(s_rfid_unlock);
    ui_make_topbar(s_rfid_unlock, "RFID", rfid_back_to_choose);

    lv_obj_t *card = lv_obj_create(s_rfid_unlock);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(card, 760, 300);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_radius(card, 24, 0);
    lv_obj_set_style_bg_color(card, c_card(), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x233152), 0);
    lv_obj_set_style_pad_all(card, 22, 0);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "Tap your card");
    lv_obj_set_style_text_color(title, c_text(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_rfid_last_uid = lv_label_create(card);
    lv_label_set_text(s_rfid_last_uid, "UID ----");
    lv_obj_set_style_text_color(s_rfid_last_uid, c_sub(), 0);
    lv_obj_set_style_text_font(s_rfid_last_uid, &lv_font_montserrat_16, 0);
    lv_obj_align(s_rfid_last_uid, LV_ALIGN_TOP_LEFT, 0, 44);

    lv_obj_t *btn = lv_btn_create(card);
    lv_obj_set_size(btn, 260, 56);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_radius(btn, 18, 0);
    lv_obj_set_style_bg_color(btn, c_accent(), 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, rfid_verify_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, LV_SYMBOL_OK "  Scan & Unlock");
    lv_obj_set_style_text_color(bl, lv_color_white(), 0);
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_16, 0);
    lv_obj_center(bl);

    lv_obj_t *btn2 = lv_btn_create(card);
    lv_obj_set_size(btn2, 180, 56);
    lv_obj_align(btn2, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_radius(btn2, 18, 0);
    lv_obj_set_style_bg_color(btn2, c_card2(), 0);
    lv_obj_set_style_border_width(btn2, 1, 0);
    lv_obj_set_style_border_color(btn2, lv_color_hex(0x233152), 0);
    lv_obj_add_event_cb(btn2, rfid_open_manage_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *bl2 = lv_label_create(btn2);
    lv_label_set_text(bl2, LV_SYMBOL_SETTINGS "  Manage");
    lv_obj_set_style_text_color(bl2, c_text(), 0);
    lv_obj_set_style_text_font(bl2, &lv_font_montserrat_16, 0);
    lv_obj_center(bl2);

    s_rfid_unlock_status = lv_label_create(card);
    lv_label_set_text(s_rfid_unlock_status, "Ready");
    lv_obj_set_style_text_color(s_rfid_unlock_status, c_sub(), 0);
    lv_obj_set_style_text_font(s_rfid_unlock_status, &lv_font_montserrat_14, 0);
    lv_obj_align(s_rfid_unlock_status, LV_ALIGN_BOTTOM_MID, 0, -76);
}

static void build_rfid_manage(void)
{
    s_rfid_manage = lv_obj_create(NULL);
    style_screen(s_rfid_manage);
    ui_make_topbar(s_rfid_manage, "RFID · Manage", rfid_back_to_unlock);

    lv_obj_t *card = lv_obj_create(s_rfid_manage);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(card, 760, 360);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 12);
    lv_obj_set_style_radius(card, 24, 0);
    lv_obj_set_style_bg_color(card, c_card(), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x24304A), 0);
    lv_obj_set_style_pad_all(card, 22, 0);
    lv_obj_set_style_pad_gap(card, 12, 0);

    lv_obj_t *info = lv_label_create(card);
    lv_label_set_text(info, "Tap card to enroll. Tap list to select, then delete.");
    lv_obj_set_style_text_color(info, c_sub(), 0);
    lv_obj_set_style_text_font(info, &lv_font_montserrat_14, 0);
    lv_obj_align(info, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *btn1 = lv_btn_create(card);
    lv_obj_set_size(btn1, 240, 54);
    lv_obj_align(btn1, LV_ALIGN_TOP_LEFT, 0, 34);
    lv_obj_set_style_radius(btn1, 18, 0);
    lv_obj_set_style_bg_color(btn1, c_accent(), 0);
    lv_obj_set_style_border_width(btn1, 0, 0);
    lv_obj_add_event_cb(btn1, rfid_scan_enroll_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *t1 = lv_label_create(btn1);
    lv_label_set_text(t1, LV_SYMBOL_PLUS "  Scan to Enroll");
    lv_obj_set_style_text_color(t1, lv_color_white(), 0);
    lv_obj_set_style_text_font(t1, &lv_font_montserrat_16, 0);
    lv_obj_center(t1);

    lv_obj_t *btn_refresh = lv_btn_create(card);
    lv_obj_set_size(btn_refresh, 200, 54);
    lv_obj_align(btn_refresh, LV_ALIGN_TOP_LEFT, 270, 34);
    lv_obj_set_style_radius(btn_refresh, 18, 0);
    lv_obj_set_style_bg_color(btn_refresh, c_card2(), 0);
    lv_obj_set_style_border_width(btn_refresh, 1, 0);
    lv_obj_set_style_border_color(btn_refresh, lv_color_hex(0x24304A), 0);
    lv_obj_add_event_cb(btn_refresh, rfid_manage_refresh_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *tr = lv_label_create(btn_refresh);
    lv_label_set_text(tr, LV_SYMBOL_REFRESH "  Refresh");
    lv_obj_set_style_text_color(tr, c_text(), 0);
    lv_obj_set_style_text_font(tr, &lv_font_montserrat_16, 0);
    lv_obj_center(tr);

    lv_obj_t *btn_clear = lv_btn_create(card);
    lv_obj_set_size(btn_clear, 200, 54);
    lv_obj_align(btn_clear, LV_ALIGN_TOP_LEFT, 500, 34);
    lv_obj_set_style_radius(btn_clear, 18, 0);
    lv_obj_set_style_bg_color(btn_clear, c_card2(), 0);
    lv_obj_set_style_border_width(btn_clear, 1, 0);
    lv_obj_set_style_border_color(btn_clear, lv_color_hex(0x24304A), 0);
    lv_obj_add_event_cb(btn_clear, rfid_clear_all_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *tc = lv_label_create(btn_clear);
    lv_label_set_text(tc, LV_SYMBOL_CLOSE "  Clear All");
    lv_obj_set_style_text_color(tc, c_text(), 0);
    lv_obj_set_style_text_font(tc, &lv_font_montserrat_16, 0);
    lv_obj_center(tc);

    s_rfid_list = lv_obj_create(card);
    lv_obj_clear_flag(s_rfid_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_rfid_list, 716, 188);
    lv_obj_align(s_rfid_list, LV_ALIGN_TOP_LEFT, 0, 98);
    lv_obj_set_style_bg_opa(s_rfid_list, LV_OPA_0, 0);
    lv_obj_set_style_border_width(s_rfid_list, 0, 0);
    lv_obj_set_style_pad_all(s_rfid_list, 0, 0);
    lv_obj_set_style_pad_gap(s_rfid_list, 12, 0);
    lv_obj_set_flex_flow(s_rfid_list, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_rfid_list, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *page_bar = lv_obj_create(card);
    lv_obj_clear_flag(page_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(page_bar, 716, 46);
    lv_obj_align(page_bar, LV_ALIGN_TOP_LEFT, 0, 298);
    lv_obj_set_style_bg_opa(page_bar, LV_OPA_0, 0);
    lv_obj_set_style_border_width(page_bar, 0, 0);
    lv_obj_set_style_pad_all(page_bar, 0, 0);

    lv_obj_t *btn_prev = lv_btn_create(page_bar);
    lv_obj_set_size(btn_prev, 120, 42);
    lv_obj_align(btn_prev, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(btn_prev, 16, 0);
    lv_obj_set_style_bg_color(btn_prev, c_card2(), 0);
    lv_obj_set_style_border_width(btn_prev, 1, 0);
    lv_obj_set_style_border_color(btn_prev, lv_color_hex(0x24304A), 0);
    lv_obj_add_event_cb(btn_prev, rfid_prev_page_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lp = lv_label_create(btn_prev);
    lv_label_set_text(lp, LV_SYMBOL_LEFT "  Prev");
    lv_obj_set_style_text_color(lp, c_text(), 0);
    lv_obj_center(lp);

    s_rfid_page_lbl = lv_label_create(page_bar);
    lv_label_set_text(s_rfid_page_lbl, "Page 1/1");
    lv_obj_set_style_text_color(s_rfid_page_lbl, c_sub(), 0);
    lv_obj_set_style_text_font(s_rfid_page_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(s_rfid_page_lbl, LV_ALIGN_CENTER, 0, 0);

    s_rfid_sel_lbl = lv_label_create(page_bar);
    lv_label_set_text(s_rfid_sel_lbl, "Selected: 0");
    lv_obj_set_style_text_color(s_rfid_sel_lbl, c_sub(), 0);
    lv_obj_set_style_text_font(s_rfid_sel_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(s_rfid_sel_lbl, LV_ALIGN_RIGHT_MID, -140, 0);

    lv_obj_t *btn_next = lv_btn_create(page_bar);
    lv_obj_set_size(btn_next, 120, 42);
    lv_obj_align(btn_next, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(btn_next, 16, 0);
    lv_obj_set_style_bg_color(btn_next, c_card2(), 0);
    lv_obj_set_style_border_width(btn_next, 1, 0);
    lv_obj_set_style_border_color(btn_next, lv_color_hex(0x24304A), 0);
    lv_obj_add_event_cb(btn_next, rfid_next_page_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ln = lv_label_create(btn_next);
    lv_label_set_text(ln, "Next  " LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(ln, c_text(), 0);
    lv_obj_center(ln);

    lv_obj_t *btn_del_sel = lv_btn_create(card);
    lv_obj_set_size(btn_del_sel, 260, 56);
    lv_obj_align(btn_del_sel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_radius(btn_del_sel, 18, 0);
    lv_obj_set_style_bg_color(btn_del_sel, lv_color_hex(0x24304A), 0);
    lv_obj_set_style_border_width(btn_del_sel, 1, 0);
    lv_obj_set_style_border_color(btn_del_sel, lv_color_hex(0x24304A), 0);
    lv_obj_add_event_cb(btn_del_sel, rfid_delete_selected_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *td = lv_label_create(btn_del_sel);
    lv_label_set_text(td, LV_SYMBOL_TRASH "  Delete Selected");
    lv_obj_set_style_text_color(td, c_text(), 0);
    lv_obj_set_style_text_font(td, &lv_font_montserrat_16, 0);
    lv_obj_center(td);

    s_rfid_manage_status = lv_label_create(card);
    lv_label_set_text(s_rfid_manage_status, "Ready");
    lv_obj_set_style_text_color(s_rfid_manage_status, c_sub(), 0);
    lv_obj_set_style_text_font(s_rfid_manage_status, &lv_font_montserrat_14, 0);
    lv_obj_align(s_rfid_manage_status, LV_ALIGN_BOTTOM_LEFT, 0, -64);

    rfid_manage_rebuild_list_ui();
}

static void choose_to_fp(lv_event_t *e) { (void)e; nav_to(s_fp_unlock); }
static void choose_to_rfid(lv_event_t *e) { (void)e; nav_to(s_rfid_unlock); }
static void choose_to_pin(lv_event_t *e) { (void)e; nav_to(s_pin_unlock); }
static void choose_back_home(lv_event_t *e) { (void)e; nav_to(s_home); }

static void build_choose(void)
{
    s_choose = lv_obj_create(NULL);
    style_screen(s_choose);
    ui_make_topbar(s_choose, "Unlock", choose_back_home);

    lv_obj_t *wrap = lv_obj_create(s_choose);
    lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(wrap, 760, 340);
    lv_obj_align(wrap, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_bg_opa(wrap, LV_OPA_0, 0);
    lv_obj_set_style_border_width(wrap, 0, 0);
    lv_obj_set_style_pad_all(wrap, 0, 0);
    lv_obj_set_style_pad_gap(wrap, 18, 0);
    lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wrap, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *b1 = ui_make_card_button(wrap, "Fingerprint", "AS608");
    lv_obj_set_size(b1, 240, 160);
    lv_obj_add_event_cb(b1, choose_to_fp, LV_EVENT_CLICKED, NULL);

    lv_obj_t *b2 = ui_make_card_button(wrap, "RFID", "MFRC522");
    lv_obj_set_size(b2, 240, 160);
    lv_obj_add_event_cb(b2, choose_to_rfid, LV_EVENT_CLICKED, NULL);

    lv_obj_t *b3 = ui_make_card_button(wrap, "Password", "Keypad");
    lv_obj_set_size(b3, 240, 160);
    lv_obj_add_event_cb(b3, choose_to_pin, LV_EVENT_CLICKED, NULL);

    lv_obj_t *note = lv_label_create(s_choose);
    lv_label_set_text(note, "Dev tip: enroll at least one fingerprint/card before verify.");
    lv_obj_set_style_text_color(note, c_sub(), 0);
    lv_obj_set_style_text_font(note, &lv_font_montserrat_14, 0);
    lv_obj_align(note, LV_ALIGN_BOTTOM_MID, 0, -18);
}

void ui_lock_init(void)
{
    LOG_I("UI_LOCK", "init");

    lock_data_init();
    
    if (lock_pin_count() == 0) {
        (void)lock_pin_add("1234", 0, "default", NULL);
    }

    /* 初始化 AS608 服务（异步 worker 会用到） */
    AS608_Port_BindUart(&huart4);
    as608_svc_rc_t rc = AS608_Service_Init(0xFFFFFFFF, 0x00000000);
    LOG_I("UI_LOCK", "AS608_Service_Init rc=%d", (int)rc);

    /* Home 使用独立配色；其余子页面使用子页面配色。 */
    ui_use_palette(&s_pal_home);
    build_home();

    ui_use_palette(&s_pal_sub);
    build_choose();
    build_fp_unlock();
    build_fp_manage();
    build_rfid_unlock();
    build_rfid_manage();
    build_pin_unlock();
    build_pin_manage();

    if (!s_fp_q) s_fp_q = xQueueCreate(4, sizeof(fp_cmd_t));
    if (s_fp_q && !s_fp_task) {
        (void)xTaskCreate(fp_worker, "fp_worker", 1024, NULL, (tskIDLE_PRIORITY + 2), &s_fp_task);
    }

    if (!s_rfid_q) s_rfid_q = xQueueCreate(4, sizeof(rfid_cmd_t));
    if (s_rfid_q && !s_rfid_task) {
        (void)xTaskCreate(rfid_worker, "rfid_worker", 1024, NULL, (tskIDLE_PRIORITY + 2), &s_rfid_task);
    }

    nav_to(s_home);
}
