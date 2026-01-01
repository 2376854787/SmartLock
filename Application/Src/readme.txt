SmartLock - LVGL/UI quick notes (ASCII; safe for Windows codepages)

1) LVGL startup path
- `Application/Src/lvgl_task.c`
  - `lvgl_tick_task`: calls `lv_tick_inc()` periodically (LVGL timebase)
  - `lvgl_handler_task`: `lv_init()` -> `lv_port_disp_init()`/`lv_port_indev_init()` -> `ui_lock_init()` -> loop `lv_timer_handler()`

2) UI entry
- `Application/Src/lvgl_task.c`: calls `ui_lock_init()` after LVGL is ready
- `Application/Src/ui_lock.c`: multimodal smart-lock UI (dev stage)
  - Home (time/date/weather placeholders) -> tap to enter
  - Choose: Fingerprint / RFID / Password(PIN)
  - Each method has an unlock page + a manage(CRUD) page
- `Application/Inc/lock_data.h` + `Application/Src/lock_data.c`: in-RAM credential store (RFID cards + PINs) with stable IDs, designed for future MQTT/cloud sync

3) Touch mapping (landscape mirror / wrong hitbox)
- LVGL indev: `Drivers/BSP/lvgl_port/lvgl_port.c` -> `gt9147_read_point()`
- Raw->screen mapping: `Drivers/BSP/touch/gt9xxx.c` (`gt9xxx_map_point()`)
  - Landscape swaps axes via `tp_dev.touchtype & 0x01`
  - Fix left/right or up/down mirroring with:
    - `TOUCH_LANDSCAPE_INVERT_X`
    - `TOUCH_LANDSCAPE_INVERT_Y`

4) LVGL config macros used by this UI
- `Middlewares/LVGL/lv_conf.h`
  - `LV_USE_BTNMATRIX`: PIN keypad uses `lv_btnmatrix`
  - `LV_USE_FLEX`: choose-method page uses flex layout
  - `LV_FONT_MONTSERRAT_20` / `LV_FONT_MONTSERRAT_48`: titles + big time

IMPORTANT build note
This project manually lists LVGL sources in `CMakeLists.txt`.
If you enable a widget/font in `lv_conf.h`, also add its `*.c` to `CMakeLists.txt`.

More notes:
- `Middlewares/LVGL/PROJECT_NOTES.md`
- `Drivers/BSP/lvgl_port/AI_NOTES.md`
- `Drivers/BSP/touch/AI_NOTES.md`
