#include "ui_fingerprint.h"
#include "lvgl.h"
#include "as608_service.h"
#include "lock_devices.h"
#include "lvgl_task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "log.h"

static lv_obj_t *main_screen = NULL;
static lv_obj_t *enroll_screen = NULL;
static lv_obj_t *verify_screen = NULL;
static lv_obj_t *manage_screen = NULL;

static lv_obj_t *main_status_label = NULL;
static lv_obj_t *enroll_status_label = NULL;
static lv_obj_t *verify_status_label = NULL;
static lv_obj_t *manage_status_label = NULL;

static lv_obj_t *verify_id_label = NULL;
static lv_obj_t *verify_score_label = NULL;
static lv_obj_t *id_input = NULL;
static lv_obj_t *id_display = NULL;

typedef enum {
    FP_OP_ENROLL = 0,
    FP_OP_VERIFY,
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
    as608_status_t status;
    uint16_t found_id;
    uint16_t score;
} fp_result_t;

static QueueHandle_t s_fp_cmd_q = NULL;
static TaskHandle_t s_fp_worker_task = NULL;
static volatile bool s_fp_busy = false;
static uint16_t s_fp_capacity = 162;

static void fp_worker_task(void *arg);
static void fp_lvgl_apply_result_cb(void *user_data);
static void fp_set_label_text_color(lv_obj_t *label, const char *text, lv_color_t color);
static bool fp_submit(fp_cmd_t cmd);
static void fp_set_busy(bool busy);
static const char *fp_rc_str(as608_svc_rc_t rc);
static const char *fp_status_str(as608_status_t st);

static void create_main_screen(void);
static void create_enroll_screen(void);
static void create_verify_screen(void);
static void create_manage_screen(void);

void enroll_btn_event_handler(lv_event_t *e);
void verify_btn_event_handler(lv_event_t *e);
void manage_btn_event_handler(lv_event_t *e);
void back_btn_event_handler(lv_event_t *e);
void enroll_start_event_handler(lv_event_t *e);
void verify_start_event_handler(lv_event_t *e);
void delete_id_event_handler(lv_event_t *e);
void clear_library_event_handler(lv_event_t *e);

static void create_main_screen(void) {
    main_screen = lv_obj_create(NULL);
    
    lv_obj_t *title = lv_label_create(main_screen);
    lv_label_set_text(title, "Smart Clock");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);
    
    lv_obj_t *btn_enroll = lv_btn_create(main_screen);
    lv_obj_set_size(btn_enroll, 180, 60);
    lv_obj_align(btn_enroll, LV_ALIGN_CENTER, 0, -60);
    lv_obj_t *label_enroll = lv_label_create(btn_enroll);
    lv_label_set_text(label_enroll, "Enroll");
    lv_obj_center(label_enroll);
    lv_obj_add_event_cb(btn_enroll, enroll_btn_event_handler, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *btn_verify = lv_btn_create(main_screen);
    lv_obj_set_size(btn_verify, 180, 60);
    lv_obj_align(btn_verify, LV_ALIGN_CENTER, 0, 20);
    lv_obj_t *label_verify = lv_label_create(btn_verify);
    lv_label_set_text(label_verify, "Verify");
    lv_obj_center(label_verify);
    lv_obj_add_event_cb(btn_verify, verify_btn_event_handler, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *btn_manage = lv_btn_create(main_screen);
    lv_obj_set_size(btn_manage, 180, 60);
    lv_obj_align(btn_manage, LV_ALIGN_CENTER, 0, 100);
    lv_obj_t *label_manage = lv_label_create(btn_manage);
    lv_label_set_text(label_manage, "Manage");
    lv_obj_center(label_manage);
    lv_obj_add_event_cb(btn_manage, manage_btn_event_handler, LV_EVENT_CLICKED, NULL);
    
    main_status_label = lv_label_create(main_screen);
    lv_label_set_text(main_status_label, "Ready");
    lv_obj_align(main_status_label, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_set_style_text_color(main_status_label, lv_color_hex(0x00FF00), 0);
}

static void create_enroll_screen(void) {
    enroll_screen = lv_obj_create(NULL);
    
    lv_obj_t *title = lv_label_create(enroll_screen);
    lv_label_set_text(title, "Fingerprint Enroll");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
    
    lv_obj_t *id_label_title = lv_label_create(enroll_screen);
    lv_label_set_text(id_label_title, "ID:");
    lv_obj_align(id_label_title, LV_ALIGN_TOP_MID, -50, 80);
    
    id_input = lv_textarea_create(enroll_screen);
    lv_obj_set_size(id_input, 100, 40);
    lv_textarea_set_one_line(id_input, true);
    lv_textarea_set_max_length(id_input, 3);
    lv_textarea_set_text(id_input, "1");
    lv_obj_align(id_input, LV_ALIGN_TOP_MID, 20, 80);
    
    lv_obj_t *btn_back = lv_btn_create(enroll_screen);
    lv_obj_set_size(btn_back, 120, 50);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 20, -20);
    lv_obj_t *label_back = lv_label_create(btn_back);
    lv_label_set_text(label_back, "Back");
    lv_obj_center(label_back);
    lv_obj_add_event_cb(btn_back, back_btn_event_handler, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *btn_start = lv_btn_create(enroll_screen);
    lv_obj_set_size(btn_start, 120, 50);
    lv_obj_align(btn_start, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
    lv_obj_t *label_start = lv_label_create(btn_start);
    lv_label_set_text(label_start, "Start");
    lv_obj_center(label_start);
    lv_obj_add_event_cb(btn_start, enroll_start_event_handler, LV_EVENT_CLICKED, NULL);
    
    enroll_status_label = lv_label_create(enroll_screen);
    lv_label_set_text(enroll_status_label, "Place finger on sensor");
    lv_obj_align(enroll_status_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(enroll_status_label, lv_color_hex(0xFFFF00), 0);
}

static void create_verify_screen(void) {
    verify_screen = lv_obj_create(NULL);
    
    lv_obj_t *title = lv_label_create(verify_screen);
    lv_label_set_text(title, "Fingerprint Verify");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
    
    verify_id_label = lv_label_create(verify_screen);
    lv_label_set_text(verify_id_label, "ID: -");
    lv_obj_align(verify_id_label, LV_ALIGN_TOP_MID, 0, 80);
    
    verify_score_label = lv_label_create(verify_screen);
    lv_label_set_text(verify_score_label, "Score: -");
    lv_obj_align(verify_score_label, LV_ALIGN_TOP_MID, 0, 120);
    
    lv_obj_t *btn_back = lv_btn_create(verify_screen);
    lv_obj_set_size(btn_back, 120, 50);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 20, -20);
    lv_obj_t *label_back = lv_label_create(btn_back);
    lv_label_set_text(label_back, "Back");
    lv_obj_center(label_back);
    lv_obj_add_event_cb(btn_back, back_btn_event_handler, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *btn_start = lv_btn_create(verify_screen);
    lv_obj_set_size(btn_start, 120, 50);
    lv_obj_align(btn_start, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
    lv_obj_t *label_start = lv_label_create(btn_start);
    lv_label_set_text(label_start, "Verify");
    lv_obj_center(label_start);
    lv_obj_add_event_cb(btn_start, verify_start_event_handler, LV_EVENT_CLICKED, NULL);
    
    verify_status_label = lv_label_create(verify_screen);
    lv_label_set_text(verify_status_label, "Place finger on sensor");
    lv_obj_align(verify_status_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(verify_status_label, lv_color_hex(0xFFFF00), 0);
}

static void create_manage_screen(void) {
    manage_screen = lv_obj_create(NULL);
    
    lv_obj_t *title = lv_label_create(manage_screen);
    lv_label_set_text(title, "Fingerprint Manage");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
    
    lv_obj_t *id_label_title = lv_label_create(manage_screen);
    lv_label_set_text(id_label_title, "ID:");
    lv_obj_align(id_label_title, LV_ALIGN_TOP_MID, -50, 70);
    
    id_display = lv_textarea_create(manage_screen);
    lv_obj_set_size(id_display, 100, 40);
    lv_textarea_set_one_line(id_display, true);
    lv_textarea_set_max_length(id_display, 3);
    lv_textarea_set_text(id_display, "1");
    lv_obj_align(id_display, LV_ALIGN_TOP_MID, 20, 70);
    
    lv_obj_t *btn_delete = lv_btn_create(manage_screen);
    lv_obj_set_size(btn_delete, 180, 50);
    lv_obj_align(btn_delete, LV_ALIGN_CENTER, 0, -30);
    lv_obj_t *label_delete = lv_label_create(btn_delete);
    lv_label_set_text(label_delete, "Delete ID");
    lv_obj_center(label_delete);
    lv_obj_add_event_cb(btn_delete, delete_id_event_handler, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *btn_clear = lv_btn_create(manage_screen);
    lv_obj_set_size(btn_clear, 180, 50);
    lv_obj_align(btn_clear, LV_ALIGN_CENTER, 0, 30);
    lv_obj_t *label_clear = lv_label_create(btn_clear);
    lv_label_set_text(label_clear, "Clear Library");
    lv_obj_center(label_clear);
    lv_obj_add_event_cb(btn_clear, clear_library_event_handler, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *btn_back = lv_btn_create(manage_screen);
    lv_obj_set_size(btn_back, 120, 50);
    lv_obj_align(btn_back, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_t *label_back = lv_label_create(btn_back);
    lv_label_set_text(label_back, "Back");
    lv_obj_center(label_back);
    lv_obj_add_event_cb(btn_back, back_btn_event_handler, LV_EVENT_CLICKED, NULL);
    
    manage_status_label = lv_label_create(manage_screen);
    lv_label_set_text(manage_status_label, "Ready");
    lv_obj_align(manage_status_label, LV_ALIGN_BOTTOM_MID, 0, -80);
    lv_obj_set_style_text_color(manage_status_label, lv_color_hex(0x00FF00), 0);
}

void ui_fingerprint_init(void) {
    if (!LockDevices_WaitAs608Ready(5000u)) {
        LOG_E("UI_FP", "AS608 not ready");
    }

    s_fp_capacity = AS608_Get_Capacity();
    if (s_fp_capacity == 0) {
        s_fp_capacity = 162;
    }

    if (s_fp_cmd_q == NULL) {
        s_fp_cmd_q = xQueueCreate(4, sizeof(fp_cmd_t));
        if (s_fp_cmd_q == NULL) {
            LOG_E("UI_FP", "xQueueCreate(fp_cmd_q) failed");
        }
    }
    if ((s_fp_worker_task == NULL) && (s_fp_cmd_q != NULL)) {
        BaseType_t ok = xTaskCreate(fp_worker_task, "fp_worker", 768, NULL, (tskIDLE_PRIORITY + 2), &s_fp_worker_task);
        if (ok != pdPASS) {
            LOG_E("UI_FP", "xTaskCreate(fp_worker) failed");
        }
    }

    create_main_screen();
    create_enroll_screen();
    create_verify_screen();
    create_manage_screen();
    
    lv_scr_load(main_screen);
    printf("[UI] Main screen loaded\n");
}

void enroll_btn_event_handler(lv_event_t *e) {
    lv_scr_load(enroll_screen);
}

void verify_btn_event_handler(lv_event_t *e) {
    lv_scr_load(verify_screen);
}

void manage_btn_event_handler(lv_event_t *e) {
    lv_scr_load(manage_screen);
}

void back_btn_event_handler(lv_event_t *e) {
    lv_scr_load(main_screen);
}

void enroll_start_event_handler(lv_event_t *e) {
    (void)e;
    const char *id_str = lv_textarea_get_text(id_input);
    uint16_t id = (uint16_t)atoi(id_str);
    if (id < 1 || id >= s_fp_capacity) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Invalid ID (1-%u)", (unsigned)(s_fp_capacity - 1u));
        fp_set_label_text_color(enroll_status_label, buf, lv_color_hex(0xFF0000));
        return;
    }

    if (s_fp_busy) {
        fp_set_label_text_color(enroll_status_label, "Busy...", lv_color_hex(0xFF0000));
        return;
    }

    LOG_I("UI_FP", "Enroll start id=%u", (unsigned)id);
    fp_set_label_text_color(enroll_status_label, "Enrolling... (press finger twice)", lv_color_hex(0xFFFF00));
    fp_set_busy(true);
    if (!fp_submit((fp_cmd_t){.op = FP_OP_ENROLL, .id = id})) {
        fp_set_label_text_color(enroll_status_label, "Queue full", lv_color_hex(0xFF0000));
        fp_set_busy(false);
    }
}

void verify_start_event_handler(lv_event_t *e) {
    (void)e;
    if (s_fp_busy) {
        fp_set_label_text_color(verify_status_label, "Busy...", lv_color_hex(0xFF0000));
        return;
    }

    if (verify_id_label) lv_label_set_text(verify_id_label, "ID: -");
    if (verify_score_label) lv_label_set_text(verify_score_label, "Score: -");

    LOG_I("UI_FP", "Verify start");
    fp_set_label_text_color(verify_status_label, "Verifying...", lv_color_hex(0xFFFF00));
    fp_set_busy(true);
    if (!fp_submit((fp_cmd_t){.op = FP_OP_VERIFY, .id = 0})) {
        fp_set_label_text_color(verify_status_label, "Queue full", lv_color_hex(0xFF0000));
        fp_set_busy(false);
    }
}

void delete_id_event_handler(lv_event_t *e) {
    (void)e;
    const char *id_str = lv_textarea_get_text(id_display);
    uint16_t id = (uint16_t)atoi(id_str);
    if (id < 1 || id >= s_fp_capacity) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Invalid ID (1-%u)", (unsigned)(s_fp_capacity - 1u));
        fp_set_label_text_color(manage_status_label, buf, lv_color_hex(0xFF0000));
        return;
    }

    if (s_fp_busy) {
        fp_set_label_text_color(manage_status_label, "Busy...", lv_color_hex(0xFF0000));
        return;
    }

    LOG_I("UI_FP", "Delete start id=%u", (unsigned)id);
    fp_set_label_text_color(manage_status_label, "Deleting...", lv_color_hex(0xFFFF00));
    fp_set_busy(true);
    if (!fp_submit((fp_cmd_t){.op = FP_OP_DELETE, .id = id})) {
        fp_set_label_text_color(manage_status_label, "Queue full", lv_color_hex(0xFF0000));
        fp_set_busy(false);
    }
}

void clear_library_event_handler(lv_event_t *e) {
    (void)e;
    if (s_fp_busy) {
        fp_set_label_text_color(manage_status_label, "Busy...", lv_color_hex(0xFF0000));
        return;
    }

    LOG_I("UI_FP", "ClearAll start");
    fp_set_label_text_color(manage_status_label, "Clearing...", lv_color_hex(0xFFFF00));
    fp_set_busy(true);
    if (!fp_submit((fp_cmd_t){.op = FP_OP_CLEAR, .id = 0})) {
        fp_set_label_text_color(manage_status_label, "Queue full", lv_color_hex(0xFF0000));
        fp_set_busy(false);
    }
}

static void fp_set_label_text_color(lv_obj_t *label, const char *text, lv_color_t color)
{
    if (label == NULL) {
        return;
    }
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_color(label, color, 0);
}

static void fp_set_busy(bool busy)
{
    s_fp_busy = busy;
}

static bool fp_submit(fp_cmd_t cmd)
{
    if (s_fp_cmd_q == NULL) {
        LOG_E("UI_FP", "submit failed: cmd queue not created");
        return false;
    }
    return xQueueSend(s_fp_cmd_q, &cmd, 0) == pdTRUE;
}

static void fp_worker_task(void *arg)
{
    (void)arg;

    for (;;) {
        fp_cmd_t cmd;
        if (xQueueReceive(s_fp_cmd_q, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        uint32_t start = xTaskGetTickCount();
        fp_result_t *res = (fp_result_t *)pvPortMalloc(sizeof(fp_result_t));
        if (res == NULL) {
            lvgl_lock();
            fp_set_label_text_color(main_status_label, "OOM", lv_color_hex(0xFF0000));
            lvgl_unlock();
            fp_set_busy(false);
            continue;
        }

        memset(res, 0, sizeof(*res));
        res->op = cmd.op;
        res->id = cmd.id;
        res->status = AS608_STATUS_UNKNOWN;

        switch (cmd.op) {
            case FP_OP_ENROLL:
                LOG_I("UI_FP", "AS608 enroll id=%u", (unsigned)cmd.id);
                res->rc = AS608_CRUD_Create(cmd.id, 15000, &res->status);
                break;
            case FP_OP_VERIFY:
                LOG_I("UI_FP", "AS608 verify");
                res->rc = AS608_CRUD_Read(8000, &res->found_id, &res->score, &res->status);
                break;
            case FP_OP_DELETE:
                LOG_I("UI_FP", "AS608 delete id=%u", (unsigned)cmd.id);
                res->rc = AS608_CRUD_Delete(cmd.id, &res->status);
                break;
            case FP_OP_CLEAR:
                LOG_I("UI_FP", "AS608 clear all");
                res->rc = AS608_CRUD_ClearAll(&res->status);
                break;
            default:
                res->rc = AS608_SVC_ERR;
                break;
        }

        uint32_t cost = (uint32_t)(xTaskGetTickCount() - start);
        LOG_I("UI_FP", "AS608 done op=%u rc=%d(%s) st=0x%02X(%s) cost=%lums",
              (unsigned)cmd.op, (int)res->rc, fp_rc_str(res->rc), (unsigned)res->status, fp_status_str(res->status),
              (unsigned long)cost);

        /* UI 更新必须回到 LVGL 任务上下文执行 */
        lvgl_lock();
        if (lv_async_call(fp_lvgl_apply_result_cb, res) != LV_RES_OK) {
            lvgl_unlock();
            fp_set_busy(false);
            vPortFree(res);
            continue;
        }
        lvgl_unlock();
    }
}

static void fp_lvgl_apply_result_cb(void *user_data)
{
    fp_result_t *res = (fp_result_t *)user_data;
    if (res == NULL) {
        fp_set_busy(false);
        return;
    }

    switch (res->op) {
        case FP_OP_ENROLL: {
            if (res->rc == AS608_SVC_OK && res->status == AS608_STATUS_OK) {
                char buf[64];
                snprintf(buf, sizeof(buf), "Enrolled! ID: %u", (unsigned)res->id);
                fp_set_label_text_color(enroll_status_label, buf, lv_color_hex(0x00FF00));
                fp_set_label_text_color(main_status_label, "Enroll OK", lv_color_hex(0x00FF00));
            } else if (res->rc == AS608_SVC_TIMEOUT) {
                fp_set_label_text_color(enroll_status_label, "Timeout! Try again.", lv_color_hex(0xFF0000));
            } else {
                char buf[96];
                snprintf(buf, sizeof(buf), "Enroll failed rc=%s st=%s", fp_rc_str(res->rc), fp_status_str(res->status));
                fp_set_label_text_color(enroll_status_label, buf, lv_color_hex(0xFF0000));
            }
            break;
        }
        case FP_OP_VERIFY: {
            if (res->rc == AS608_SVC_OK && res->status == AS608_STATUS_OK) {
                char buf[64];
                snprintf(buf, sizeof(buf), "ID: %u", (unsigned)res->found_id);
                if (verify_id_label) lv_label_set_text(verify_id_label, buf);
                snprintf(buf, sizeof(buf), "Score: %u", (unsigned)res->score);
                if (verify_score_label) lv_label_set_text(verify_score_label, buf);
                fp_set_label_text_color(verify_status_label, "Verified!", lv_color_hex(0x00FF00));
                fp_set_label_text_color(main_status_label, "Verify OK", lv_color_hex(0x00FF00));
            } else if (res->rc == AS608_SVC_TIMEOUT) {
                fp_set_label_text_color(verify_status_label, "Timeout", lv_color_hex(0xFF0000));
            } else {
                if (verify_id_label) lv_label_set_text(verify_id_label, "ID: -");
                if (verify_score_label) lv_label_set_text(verify_score_label, "Score: -");
                {
                    char buf[96];
                    snprintf(buf, sizeof(buf), "Fail rc=%s st=%s", fp_rc_str(res->rc), fp_status_str(res->status));
                    fp_set_label_text_color(verify_status_label, buf, lv_color_hex(0xFF0000));
                }
            }
            break;
        }
        case FP_OP_DELETE: {
            if (res->rc == AS608_SVC_OK && res->status == AS608_STATUS_OK) {
                char buf[64];
                snprintf(buf, sizeof(buf), "Deleted! ID: %u", (unsigned)res->id);
                fp_set_label_text_color(manage_status_label, buf, lv_color_hex(0x00FF00));
                fp_set_label_text_color(main_status_label, "Delete OK", lv_color_hex(0x00FF00));
            } else {
                char buf[96];
                snprintf(buf, sizeof(buf), "Delete failed rc=%s st=%s", fp_rc_str(res->rc), fp_status_str(res->status));
                fp_set_label_text_color(manage_status_label, buf, lv_color_hex(0xFF0000));
            }
            break;
        }
        case FP_OP_CLEAR: {
            if (res->rc == AS608_SVC_OK && res->status == AS608_STATUS_OK) {
                fp_set_label_text_color(manage_status_label, "Library cleared!", lv_color_hex(0x00FF00));
                fp_set_label_text_color(main_status_label, "Library cleared", lv_color_hex(0x00FF00));
            } else {
                char buf[96];
                snprintf(buf, sizeof(buf), "Clear failed rc=%s st=%s", fp_rc_str(res->rc), fp_status_str(res->status));
                fp_set_label_text_color(manage_status_label, buf, lv_color_hex(0xFF0000));
            }
            break;
        }
        default:
            break;
    }

    fp_set_busy(false);
    vPortFree(res);
}

static const char *fp_rc_str(as608_svc_rc_t rc)
{
    switch (rc) {
        case AS608_SVC_OK: return "OK";
        case AS608_SVC_ERR: return "ERR";
        case AS608_SVC_TIMEOUT: return "TIMEOUT";
        case AS608_SVC_NOT_READY: return "NOT_READY";
        default: return "UNKNOWN";
    }
}

static const char *fp_status_str(as608_status_t st)
{
    switch (st) {
        case AS608_STATUS_OK: return "OK";
        case AS608_STATUS_NO_FINGERPRINT: return "NO_FINGER";
        case AS608_STATUS_NOT_FOUND: return "NOT_FOUND";
        case AS608_STATUS_NOT_MATCH: return "NOT_MATCH";
        case AS608_STATUS_FRAME_ERROR: return "FRAME_ERROR";
        case AS608_STATUS_COMMAND_INVALID: return "CMD_INVALID";
        default: return "OTHER";
    }
}
