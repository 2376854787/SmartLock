#include "ui_lock.h"

#include "lvgl.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "as608_port.h"
#include "as608_service.h"
#include "log.h"
#include "lvgl_task.h"
#include "rc522_my.h"
#include "usart.h"

static lv_color_t c_bg(void) { return lv_color_hex(0x0B1020); }
static lv_color_t c_card(void) { return lv_color_hex(0x141B2D); }
static lv_color_t c_card2(void) { return lv_color_hex(0x0F1628); }
static lv_color_t c_text(void) { return lv_color_hex(0xE8EEF8); }
static lv_color_t c_sub(void) { return lv_color_hex(0x93A4C7); }
static lv_color_t c_accent(void) { return lv_color_hex(0x4C7DFF); }
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

/* ================= Toast ================= */

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

/* ================= Shared UI Widgets ================= */

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

/* ================= Screens ================= */

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

static void home_tick_cb(lv_timer_t *t)
{
    (void)t;
    if (lv_scr_act() != s_home) return;

    uint32_t sec = (uint32_t)(xTaskGetTickCount() / configTICK_RATE_HZ);
    uint32_t hh = (12u + (sec / 3600u)) % 24u;
    uint32_t mm = (sec / 60u) % 60u;

    char buf[32];
    snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)hh, (unsigned long)mm);
    lv_label_set_text(s_home_time, buf);
    lv_label_set_text(s_home_date, "Mon · 2026-01-01");
    lv_label_set_text(s_home_weather, "Sunny · 24°C  |  AQI 42");
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

/* Forward: builders */
static void build_home(void);
static void build_choose(void);
static void build_fp_unlock(void);
static void build_fp_manage(void);
static void build_rfid_unlock(void);
static void build_rfid_manage(void);
static void build_pin_unlock(void);

/* ================= PIN ================= */

static lv_obj_t *s_pin_status = NULL;
static lv_obj_t *s_pin_ta = NULL;
static char s_pin_value[8] = "1234"; /* dev default */

static void pin_back_to_choose(lv_event_t *e) { (void)e; nav_to(s_choose); }

static void pin_btnm_cb(lv_event_t *e)
{
    lv_obj_t *m = lv_event_get_target(e);
    const char *txt = lv_btnmatrix_get_btn_text(m, lv_btnmatrix_get_selected_btn(m));
    if (!txt) return;

    if (strcmp(txt, "OK") == 0) {
        const char *in = lv_textarea_get_text(s_pin_ta);
        if (in && strcmp(in, s_pin_value) == 0) {
            set_label(s_pin_status, "Unlocked", c_good());
            ui_toast("Unlocked (PIN)", c_good(), 1200);
            lv_textarea_set_text(s_pin_ta, "");
            nav_to(s_home);
        } else {
            set_label(s_pin_status, "Wrong PIN", c_bad());
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
    lv_obj_set_style_border_color(card, lv_color_hex(0x233152), 0);
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
    lv_obj_set_style_border_color(s_pin_ta, lv_color_hex(0x233152), 0);

    s_pin_status = lv_label_create(card);
    lv_label_set_text(s_pin_status, "Default PIN: 1234 (dev)");
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
    lv_obj_set_style_border_color(m, lv_color_hex(0x233152), LV_PART_ITEMS);
    lv_obj_set_style_text_color(m, c_text(), LV_PART_ITEMS);
    lv_obj_set_style_text_font(m, &lv_font_montserrat_16, LV_PART_ITEMS);
}

/* ================= Fingerprint (AS608) ================= */

typedef enum {
    FP_OP_VERIFY = 0,
    FP_OP_ENROLL,
    FP_OP_DELETE,
    FP_OP_CLEAR,
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
} fp_res_t;

static QueueHandle_t s_fp_q = NULL;
static TaskHandle_t s_fp_task = NULL;
static volatile bool s_fp_busy = false;

static lv_obj_t *s_fp_unlock_status = NULL;
static lv_obj_t *s_fp_manage_status = NULL;
static lv_obj_t *s_fp_id_input = NULL;

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
static void fp_open_manage_cb(lv_event_t *e) { (void)e; nav_to(s_fp_manage); }

static void fp_unlock_start_cb(lv_event_t *e)
{
    (void)e;
    set_label(s_fp_unlock_status, "Place finger on sensor…", c_warn());
    if (!fp_submit((fp_cmd_t){.op = FP_OP_VERIFY, .id = 0})) {
        set_label(s_fp_unlock_status, "Busy…", c_bad());
    }
}

static void fp_enroll_cb(lv_event_t *e)
{
    (void)e;
    const char *txt = lv_textarea_get_text(s_fp_id_input);
    uint16_t id = (uint16_t)atoi(txt ? txt : "0");
    if (id == 0) {
        set_label(s_fp_manage_status, "Invalid ID", c_bad());
        return;
    }
    set_label(s_fp_manage_status, "Enrolling… (press finger twice)", c_warn());
    if (!fp_submit((fp_cmd_t){.op = FP_OP_ENROLL, .id = id})) {
        set_label(s_fp_manage_status, "Busy…", c_bad());
    }
}

static void fp_delete_cb(lv_event_t *e)
{
    (void)e;
    const char *txt = lv_textarea_get_text(s_fp_id_input);
    uint16_t id = (uint16_t)atoi(txt ? txt : "0");
    if (id == 0) {
        set_label(s_fp_manage_status, "Invalid ID", c_bad());
        return;
    }
    set_label(s_fp_manage_status, "Deleting…", c_warn());
    if (!fp_submit((fp_cmd_t){.op = FP_OP_DELETE, .id = id})) {
        set_label(s_fp_manage_status, "Busy…", c_bad());
    }
}

static void fp_clear_cb(lv_event_t *e)
{
    (void)e;
    set_label(s_fp_manage_status, "Clearing…", c_warn());
    if (!fp_submit((fp_cmd_t){.op = FP_OP_CLEAR, .id = 0})) {
        set_label(s_fp_manage_status, "Busy…", c_bad());
    }
}

static void fp_apply_cb(void *user_data)
{
    fp_res_t *res = (fp_res_t *)user_data;
    if (!res) {
        s_fp_busy = false;
        return;
    }

    char buf[96];
    switch (res->op) {
        case FP_OP_VERIFY:
            if (res->rc == AS608_SVC_OK && res->st == AS608_STATUS_OK) {
                snprintf(buf, sizeof(buf), "Match: ID %u  (score %u)", (unsigned)res->found_id, (unsigned)res->score);
                set_label(s_fp_unlock_status, buf, c_good());
                ui_toast("Unlocked (Fingerprint)", c_good(), 1200);
                nav_to(s_home);
            } else {
                snprintf(buf, sizeof(buf), "Verify fail: rc=%s st=%s", fp_rc_str(res->rc), fp_st_str(res->st));
                set_label(s_fp_unlock_status, buf, c_bad());
            }
            break;
        case FP_OP_ENROLL:
            if (res->rc == AS608_SVC_OK && res->st == AS608_STATUS_OK) {
                snprintf(buf, sizeof(buf), "Enrolled: ID %u", (unsigned)res->id);
                set_label(s_fp_manage_status, buf, c_good());
                ui_toast("Fingerprint enrolled", c_good(), 1200);
            } else {
                snprintf(buf, sizeof(buf), "Enroll fail: rc=%s st=%s", fp_rc_str(res->rc), fp_st_str(res->st));
                set_label(s_fp_manage_status, buf, c_bad());
            }
            break;
        case FP_OP_DELETE:
            if (res->rc == AS608_SVC_OK && res->st == AS608_STATUS_OK) {
                snprintf(buf, sizeof(buf), "Deleted: ID %u", (unsigned)res->id);
                set_label(s_fp_manage_status, buf, c_good());
            } else {
                snprintf(buf, sizeof(buf), "Delete fail: rc=%s st=%s", fp_rc_str(res->rc), fp_st_str(res->st));
                set_label(s_fp_manage_status, buf, c_bad());
            }
            break;
        case FP_OP_CLEAR:
            if (res->rc == AS608_SVC_OK && res->st == AS608_STATUS_OK) {
                set_label(s_fp_manage_status, "Cleared all fingerprints", c_good());
            } else {
                snprintf(buf, sizeof(buf), "Clear fail: rc=%s st=%s", fp_rc_str(res->rc), fp_st_str(res->st));
                set_label(s_fp_manage_status, buf, c_bad());
            }
            break;
        default:
            break;
    }

    s_fp_busy = false;
    vPortFree(res);
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
    lv_obj_set_size(card, 760, 340);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_radius(card, 24, 0);
    lv_obj_set_style_bg_color(card, c_card(), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x233152), 0);
    lv_obj_set_style_pad_all(card, 22, 0);

    lv_obj_t *row = lv_obj_create(card);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, lv_pct(100), 62);
    lv_obj_align(row, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, "ID");
    lv_obj_set_style_text_color(lbl, c_sub(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

    s_fp_id_input = lv_textarea_create(row);
    lv_obj_set_size(s_fp_id_input, 140, 44);
    lv_obj_align(s_fp_id_input, LV_ALIGN_LEFT_MID, 44, 0);
    lv_textarea_set_one_line(s_fp_id_input, true);
    lv_textarea_set_text(s_fp_id_input, "1");
    lv_obj_set_style_radius(s_fp_id_input, 14, 0);
    lv_obj_set_style_bg_color(s_fp_id_input, c_card2(), 0);
    lv_obj_set_style_text_color(s_fp_id_input, c_text(), 0);
    lv_obj_set_style_border_width(s_fp_id_input, 1, 0);
    lv_obj_set_style_border_color(s_fp_id_input, lv_color_hex(0x233152), 0);

    lv_obj_t *btn_en = lv_btn_create(card);
    lv_obj_set_size(btn_en, 220, 56);
    lv_obj_align(btn_en, LV_ALIGN_TOP_LEFT, 0, 86);
    lv_obj_set_style_radius(btn_en, 18, 0);
    lv_obj_set_style_bg_color(btn_en, c_accent(), 0);
    lv_obj_set_style_border_width(btn_en, 0, 0);
    lv_obj_add_event_cb(btn_en, fp_enroll_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *t1 = lv_label_create(btn_en);
    lv_label_set_text(t1, LV_SYMBOL_PLUS "  Enroll");
    lv_obj_set_style_text_color(t1, lv_color_white(), 0);
    lv_obj_center(t1);

    lv_obj_t *btn_del = lv_btn_create(card);
    lv_obj_set_size(btn_del, 220, 56);
    lv_obj_align(btn_del, LV_ALIGN_TOP_LEFT, 250, 86);
    lv_obj_set_style_radius(btn_del, 18, 0);
    lv_obj_set_style_bg_color(btn_del, c_card2(), 0);
    lv_obj_set_style_border_width(btn_del, 1, 0);
    lv_obj_set_style_border_color(btn_del, lv_color_hex(0x233152), 0);
    lv_obj_add_event_cb(btn_del, fp_delete_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *t2 = lv_label_create(btn_del);
    lv_label_set_text(t2, LV_SYMBOL_TRASH "  Delete");
    lv_obj_set_style_text_color(t2, c_text(), 0);
    lv_obj_center(t2);

    lv_obj_t *btn_clr = lv_btn_create(card);
    lv_obj_set_size(btn_clr, 220, 56);
    lv_obj_align(btn_clr, LV_ALIGN_TOP_LEFT, 500, 86);
    lv_obj_set_style_radius(btn_clr, 18, 0);
    lv_obj_set_style_bg_color(btn_clr, c_card2(), 0);
    lv_obj_set_style_border_width(btn_clr, 1, 0);
    lv_obj_set_style_border_color(btn_clr, lv_color_hex(0x233152), 0);
    lv_obj_add_event_cb(btn_clr, fp_clear_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *t3 = lv_label_create(btn_clr);
    lv_label_set_text(t3, LV_SYMBOL_CLOSE "  Clear All");
    lv_obj_set_style_text_color(t3, c_text(), 0);
    lv_obj_center(t3);

    s_fp_manage_status = lv_label_create(card);
    lv_label_set_text(s_fp_manage_status, "Ready");
    lv_obj_set_style_text_color(s_fp_manage_status, c_sub(), 0);
    lv_obj_set_style_text_font(s_fp_manage_status, &lv_font_montserrat_14, 0);
    lv_obj_align(s_fp_manage_status, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

/* ================= RFID (MFRC522) ================= */

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

#define RFID_DB_MAX 16u
static uint8_t s_rfid_db[RFID_DB_MAX][4];
static uint8_t s_rfid_db_count = 0;

static int rfid_db_find(const uint8_t uid[4])
{
    for (uint8_t i = 0; i < s_rfid_db_count; i++) {
        if (memcmp(s_rfid_db[i], uid, 4) == 0) return (int)i;
    }
    return -1;
}

static bool rfid_db_add(const uint8_t uid[4])
{
    if (s_rfid_db_count >= RFID_DB_MAX) return false;
    if (rfid_db_find(uid) >= 0) return true;
    memcpy(s_rfid_db[s_rfid_db_count++], uid, 4);
    return true;
}

static bool rfid_db_remove(const uint8_t uid[4])
{
    int idx = rfid_db_find(uid);
    if (idx < 0) return false;
    for (uint8_t i = (uint8_t)idx; (i + 1u) < s_rfid_db_count; i++) {
        memcpy(s_rfid_db[i], s_rfid_db[i + 1u], 4);
    }
    s_rfid_db_count--;
    return true;
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
            s_rfid_db_count = 0;
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
                    if (s_rfid_db_count == 0) {
                        res->ok = false;
                        snprintf(res->msg, sizeof(res->msg), "No cards enrolled");
                    } else if (rfid_db_find(uid) >= 0) {
                        res->ok = true;
                        snprintf(res->msg, sizeof(res->msg), "Card accepted");
                    } else {
                        res->ok = false;
                        snprintf(res->msg, sizeof(res->msg), "Card not authorized");
                    }
                } else if (cmd.op == RFID_OP_ENROLL) {
                    res->ok = rfid_db_add(uid);
                    snprintf(res->msg, sizeof(res->msg), res->ok ? "Card enrolled" : "Enroll failed (DB full)");
                } else if (cmd.op == RFID_OP_DELETE) {
                    res->ok = rfid_db_remove(uid);
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
static void rfid_open_manage_cb(lv_event_t *e) { (void)e; nav_to(s_rfid_manage); }

static void rfid_verify_cb(lv_event_t *e)
{
    (void)e;
    set_label(s_rfid_unlock_status, "Tap card to unlock…", c_warn());
    if (!rfid_submit((rfid_cmd_t){.op = RFID_OP_VERIFY})) {
        set_label(s_rfid_unlock_status, "Busy…", c_bad());
    }
}

static void rfid_enroll_cb(lv_event_t *e)
{
    (void)e;
    set_label(s_rfid_manage_status, "Tap card to enroll…", c_warn());
    if (!rfid_submit((rfid_cmd_t){.op = RFID_OP_ENROLL})) {
        set_label(s_rfid_manage_status, "Busy…", c_bad());
    }
}

static void rfid_delete_cb(lv_event_t *e)
{
    (void)e;
    set_label(s_rfid_manage_status, "Tap card to delete…", c_warn());
    if (!rfid_submit((rfid_cmd_t){.op = RFID_OP_DELETE})) {
        set_label(s_rfid_manage_status, "Busy…", c_bad());
    }
}

static void rfid_clear_cb(lv_event_t *e)
{
    (void)e;
    set_label(s_rfid_manage_status, "Clearing…", c_warn());
    if (!rfid_submit((rfid_cmd_t){.op = RFID_OP_CLEAR})) {
        set_label(s_rfid_manage_status, "Busy…", c_bad());
    }
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
            ui_toast("Unlocked (RFID)", c_good(), 1200);
            nav_to(s_home);
        }
    } else {
        set_label(s_rfid_manage_status, res->msg, col);
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
    lv_obj_set_size(card, 760, 340);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_radius(card, 24, 0);
    lv_obj_set_style_bg_color(card, c_card(), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x233152), 0);
    lv_obj_set_style_pad_all(card, 22, 0);

    lv_obj_t *info = lv_label_create(card);
    lv_label_set_text(info, "Tap card to Enroll/Delete. Stored in RAM (dev only).");
    lv_obj_set_style_text_color(info, c_sub(), 0);
    lv_obj_set_style_text_font(info, &lv_font_montserrat_14, 0);
    lv_obj_align(info, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *btn1 = lv_btn_create(card);
    lv_obj_set_size(btn1, 220, 56);
    lv_obj_align(btn1, LV_ALIGN_TOP_LEFT, 0, 66);
    lv_obj_set_style_radius(btn1, 18, 0);
    lv_obj_set_style_bg_color(btn1, c_accent(), 0);
    lv_obj_set_style_border_width(btn1, 0, 0);
    lv_obj_add_event_cb(btn1, rfid_enroll_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *t1 = lv_label_create(btn1);
    lv_label_set_text(t1, LV_SYMBOL_PLUS "  Enroll");
    lv_obj_set_style_text_color(t1, lv_color_white(), 0);
    lv_obj_center(t1);

    lv_obj_t *btn2 = lv_btn_create(card);
    lv_obj_set_size(btn2, 220, 56);
    lv_obj_align(btn2, LV_ALIGN_TOP_LEFT, 250, 66);
    lv_obj_set_style_radius(btn2, 18, 0);
    lv_obj_set_style_bg_color(btn2, c_card2(), 0);
    lv_obj_set_style_border_width(btn2, 1, 0);
    lv_obj_set_style_border_color(btn2, lv_color_hex(0x233152), 0);
    lv_obj_add_event_cb(btn2, rfid_delete_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *t2 = lv_label_create(btn2);
    lv_label_set_text(t2, LV_SYMBOL_TRASH "  Delete");
    lv_obj_set_style_text_color(t2, c_text(), 0);
    lv_obj_center(t2);

    lv_obj_t *btn3 = lv_btn_create(card);
    lv_obj_set_size(btn3, 220, 56);
    lv_obj_align(btn3, LV_ALIGN_TOP_LEFT, 500, 66);
    lv_obj_set_style_radius(btn3, 18, 0);
    lv_obj_set_style_bg_color(btn3, c_card2(), 0);
    lv_obj_set_style_border_width(btn3, 1, 0);
    lv_obj_set_style_border_color(btn3, lv_color_hex(0x233152), 0);
    lv_obj_add_event_cb(btn3, rfid_clear_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *t3 = lv_label_create(btn3);
    lv_label_set_text(t3, LV_SYMBOL_CLOSE "  Clear All");
    lv_obj_set_style_text_color(t3, c_text(), 0);
    lv_obj_center(t3);

    s_rfid_manage_status = lv_label_create(card);
    lv_label_set_text(s_rfid_manage_status, "Ready");
    lv_obj_set_style_text_color(s_rfid_manage_status, c_sub(), 0);
    lv_obj_set_style_text_font(s_rfid_manage_status, &lv_font_montserrat_14, 0);
    lv_obj_align(s_rfid_manage_status, LV_ALIGN_BOTTOM_LEFT, 0, 0);
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

    /* AS608 service init (async worker will use it) */
    AS608_Port_BindUart(&huart4);
    as608_svc_rc_t rc = AS608_Service_Init(0xFFFFFFFF, 0x00000000);
    LOG_I("UI_LOCK", "AS608_Service_Init rc=%d", (int)rc);

    build_home();
    build_choose();
    build_fp_unlock();
    build_fp_manage();
    build_rfid_unlock();
    build_rfid_manage();
    build_pin_unlock();

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
