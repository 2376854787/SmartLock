## LVGL port (display + touch)

### Files
- `Drivers/BSP/lvgl_port/lvgl_port.c`: LVGL display flush + LVGL input device (pointer) read callback.
- `Drivers/BSP/lvgl_port/lvgl_port.h`: init function declarations.

### Display pipeline
- `disp_flush()` writes LVGL rendered pixels to the LCD GRAM via `lcd_set_window()` + `lcd_write_ram_prepare()` + `LCD->LCD_RAM`.
- The draw buffer is line-based (`LVGL_BUF_LINES`), trading RAM for refresh smoothness.

Tuning:
- Increase `LVGL_BUF_LINES` if you have RAM and want smoother flushes.
- Decrease it if you hit RAM pressure.

### Touch pipeline
- LVGL indev driver uses `touchpad_read()` and reads from `gt9147_read_point()` (compat wrapper for GT9xxx).
- Raw touch -> mapped screen coordinates are handled in `Drivers/BSP/touch/gt9xxx.c` (`gt9xxx_map_point()`).

Orientation/debug:
- If you see “left/right reversed” in landscape, adjust the mapping macros in `Drivers/BSP/touch/gt9xxx.c`:
  - `TOUCH_LANDSCAPE_INVERT_X`
  - `TOUCH_LANDSCAPE_INVERT_Y`
- Debug log tag: `TOUCH` (press logs are emitted on the first press edge).

