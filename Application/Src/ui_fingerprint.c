#include "ui_fingerprint.h"
#include "lvgl.h"
#include "as608_service.h"
#include "as608_port.h"
#include "usart.h"
#include "lvgl_task.h"
#include <stdio.h>

static lv_obj_t *main_screen = NULL;
static lv_obj_t *enroll_screen = NULL;
static lv_obj_t *verify_screen = NULL;
static lv_obj_t *manage_screen = NULL;

static lv_obj_t *status_label = NULL;
static lv_obj_t *id_label = NULL;
static lv_obj_t *score_label = NULL;
static lv_obj_t *id_input = NULL;
static lv_obj_t *id_display = NULL;

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
    
    status_label = lv_label_create(main_screen);
    lv_label_set_text(status_label, "Ready");
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x00FF00), 0);
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
    
    status_label = lv_label_create(enroll_screen);
    lv_label_set_text(status_label, "Place finger on sensor");
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFFF00), 0);
}

static void create_verify_screen(void) {
    verify_screen = lv_obj_create(NULL);
    
    lv_obj_t *title = lv_label_create(verify_screen);
    lv_label_set_text(title, "Fingerprint Verify");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
    
    lv_obj_t *id_label_title = lv_label_create(verify_screen);
    lv_label_set_text(id_label_title, "ID: -");
    lv_obj_align(id_label_title, LV_ALIGN_TOP_MID, 0, 80);
    id_label = id_label_title;
    
    lv_obj_t *score_label_title = lv_label_create(verify_screen);
    lv_label_set_text(score_label_title, "Score: -");
    lv_obj_align(score_label_title, LV_ALIGN_TOP_MID, 0, 120);
    score_label = score_label_title;
    
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
    
    status_label = lv_label_create(verify_screen);
    lv_label_set_text(status_label, "Place finger on sensor");
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFFF00), 0);
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
    
    status_label = lv_label_create(manage_screen);
    lv_label_set_text(status_label, "Ready");
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -80);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x00FF00), 0);
}

void ui_fingerprint_init(void) {
    AS608_Port_BindUart(&huart4);
    AS608_Service_Init(0xFFFFFFFF, 0x00000000);
    
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
    const char *id_str = lv_textarea_get_text(id_input);
    uint16_t id = (uint16_t)atoi(id_str);
    if (id < 1 || id > 162) {
        lv_label_set_text(status_label, "Invalid ID (1-162)");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF0000), 0);
        return;
    }
    
    as608_status_t status;
    
    lv_label_set_text(status_label, "Enrolling...");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFFF00), 0);
    
    as608_svc_rc_t rc = AS608_CRUD_Create(id, 10000, &status);
    
    if (rc == AS608_SVC_OK) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Enrolled! ID: %d", id);
        lv_label_set_text(status_label, buf);
        lv_obj_set_style_text_color(status_label, lv_color_hex(0x00FF00), 0);
    } else if (rc == AS608_SVC_TIMEOUT) {
        lv_label_set_text(status_label, "Timeout! Try again.");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF0000), 0);
    } else {
        lv_label_set_text(status_label, "Enroll Failed!");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF0000), 0);
    }
}

void verify_start_event_handler(lv_event_t *e) {
    as608_status_t status;
    uint16_t found_id;
    uint16_t score;
    
    lv_label_set_text(status_label, "Verifying...");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFFF00), 0);
    
    as608_svc_rc_t rc = AS608_CRUD_Read(5000, &found_id, &score, &status);
    
    if (rc == AS608_SVC_OK) {
        char buf[64];
        snprintf(buf, sizeof(buf), "ID: %d", found_id);
        lv_label_set_text(id_label, buf);
        
        snprintf(buf, sizeof(buf), "Score: %d", score);
        lv_label_set_text(score_label, buf);
        
        lv_label_set_text(status_label, "Verified!");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0x00FF00), 0);
    } else {
        lv_label_set_text(id_label, "ID: -");
        lv_label_set_text(score_label, "Score: -");
        lv_label_set_text(status_label, "Not Found!");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF0000), 0);
    }
}

void delete_id_event_handler(lv_event_t *e) {
    const char *id_str = lv_textarea_get_text(id_display);
    uint16_t id = (uint16_t)atoi(id_str);
    if (id < 1 || id > 162) {
        lv_label_set_text(status_label, "Invalid ID (1-162)");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF0000), 0);
        return;
    }
    
    as608_status_t status;
    
    lv_label_set_text(status_label, "Deleting...");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFFF00), 0);
    
    as608_svc_rc_t rc = AS608_CRUD_Delete(id, &status);
    
    if (rc == AS608_SVC_OK) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Deleted! ID: %d", id);
        lv_label_set_text(status_label, buf);
        lv_obj_set_style_text_color(status_label, lv_color_hex(0x00FF00), 0);
    } else {
        lv_label_set_text(status_label, "Delete Failed!");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF0000), 0);
    }
}

void clear_library_event_handler(lv_event_t *e) {
    as608_status_t status;
    
    lv_label_set_text(status_label, "Clearing...");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFFF00), 0);
    
    as608_svc_rc_t rc = AS608_CRUD_ClearAll(&status);
    
    if (rc == AS608_SVC_OK) {
        lv_label_set_text(status_label, "Library Cleared!");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0x00FF00), 0);
    } else {
        lv_label_set_text(status_label, "Clear Failed!");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF0000), 0);
    }
}
