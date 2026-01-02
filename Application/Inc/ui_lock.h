#ifndef UI_LOCK_H
#define UI_LOCK_H

/* 锁屏 UI 入口。
 *
 * 调用前要求：
 * - LVGL 已初始化
 * - 显示/触摸驱动已注册
 *
 * 本工程中由 `Application/Src/lvgl_task.c`（LVGL handler 任务上下文）调用。
 */
void ui_lock_init(void);

#endif
