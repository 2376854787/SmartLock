#ifndef UI_LOCK_H
#define UI_LOCK_H

/* Lock-screen UI entry point.
 *
 * LVGL must be initialized and display/input drivers must be registered before calling this.
 * In this project it is called from `Application/Src/lvgl_task.c` (LVGL handler task context).
 */
void ui_lock_init(void);

#endif
