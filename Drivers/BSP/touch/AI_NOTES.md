## Touch (GT9xxx) notes

### Core files
- `Drivers/BSP/touch/gt9xxx.c`: GT9xxx driver + polling scan + coordinate mapping for LCD orientation.
- `Drivers/BSP/touch/touch.c`: higher-level touch API / shared state (`tp_dev`).
- `Drivers/BSP/touch/touch_port.c`: OS abstraction (delay/tick) used by the touch driver.

### Coordinate mapping (landscape/portrait)
The driver maps raw controller coordinates to LCD coordinates in `gt9xxx_map_point()`:
- Landscape is detected via `tp_dev.touchtype & 0x01` and swaps axes.
- Optional compile-time flips are provided to fix “mirror” issues:
  - `TOUCH_LANDSCAPE_INVERT_X` (left/right flip)
  - `TOUCH_LANDSCAPE_INVERT_Y` (up/down flip)
  - `TOUCH_PORTRAIT_INVERT_X`
  - `TOUCH_PORTRAIT_INVERT_Y`

If the touch press logs show the correct area but LVGL clicks the wrong widget, this mapping is the first place to check.

### Polling interval / missed taps
`gt9xxx_scan()` is throttled by `TOUCH_POLL_INTERVAL_MS` to reduce I2C load.
If you observe missed very-short taps, reduce `TOUCH_POLL_INTERVAL_MS`, or switch to an INT-driven design:
- Configure GT9xxx INT pin as EXTI.
- In the EXTI ISR, notify a high-priority touch task to read points and feed LVGL.

In this project, LVGL processing priority is controlled by `Application/Src/lvgl_task.c` macros:
- `LVGL_HANDLER_TASK_PRIORITY`
- `LVGL_TICK_TASK_PRIORITY`

