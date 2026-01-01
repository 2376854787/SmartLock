## SmartLock project notes (LVGL)

This repository uses LVGL `v8.3.6` (see `Middlewares/LVGL/lvgl.h` header comment).

### Where to start (project integration)
- LVGL task entry: `Application/Src/lvgl_task.c` (`lv_init()` + `lv_port_*_init()` + `ui_lock_init()`).
- LVGL config: `Middlewares/LVGL/lv_conf.h`.
- Display + touch port: `Drivers/BSP/lvgl_port/lvgl_port.c`.
- Touch controller driver + coordinate mapping: `Drivers/BSP/touch/gt9xxx.c`.
- Credential store (for UI + future MQTT): `Application/Inc/lock_data.h` + `Application/Src/lock_data.c`.

### Important build detail (CMake)
This project’s `CMakeLists.txt` manually lists LVGL source files.  
Enabling a widget/font in `lv_conf.h` is not enough: you must also ensure the matching `*.c` is included in `CMakeLists.txt`.

Examples (currently used by the lock UI):
- Fonts: `lv_font_montserrat_20.c`, `lv_font_montserrat_48.c`
- Layout: `src/extra/layouts/flex/lv_flex.c`
- Widget: `src/widgets/lv_btnmatrix.c` (keypad)

### Project-specific LVGL config toggles
The lock UI (`Application/Src/ui_lock.c`) expects these features to be enabled:
- `LV_USE_FLEX` (for card layout on the “choose unlock method” page)
- `LV_USE_BTNMATRIX` (for PIN numeric keypad)
- `LV_FONT_MONTSERRAT_20` and `LV_FONT_MONTSERRAT_48` (titles + large time)

If you disable any of them, either update `ui_lock.c` accordingly or re-enable the feature in `lv_conf.h` and `CMakeLists.txt`.

### RAM tuning (LVGL memory footprint)
Primary RAM consumers are:
- LVGL internal heap: `LV_MEM_SIZE` in `Middlewares/LVGL/lv_conf.h`
- Display draw buffer: `LVGL_BUF_LINES` in `Drivers/BSP/lvgl_port/lvgl_port.c`

Current tuning knobs used by this project:
- `LV_MEM_SIZE`: reduce to save RAM, increase if you see LVGL alloc/assert failures
- `LV_MEM_BUF_MAX_NUM`: internal buffer bookkeeping, smaller reduces overhead
- `LV_LAYER_SIMPLE_BUF_SIZE` / `LV_LAYER_SIMPLE_FALLBACK_BUF_SIZE`: layer draw buffers, smaller saves RAM (may reduce performance)
