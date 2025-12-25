#ifndef __UI_FINGERPRINT_H
#define __UI_FINGERPRINT_H

#include <stdint.h>
#include "lvgl.h"

void ui_fingerprint_init(void);
void enroll_btn_event_handler(lv_event_t *e);
void verify_btn_event_handler(lv_event_t *e);
void manage_btn_event_handler(lv_event_t *e);
void back_btn_event_handler(lv_event_t *e);
void enroll_start_event_handler(lv_event_t *e);
void verify_start_event_handler(lv_event_t *e);
void delete_id_event_handler(lv_event_t *e);
void clear_library_event_handler(lv_event_t *e);

#endif
