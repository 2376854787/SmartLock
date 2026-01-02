#include "lvgl_task.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "ui_lock.h"
#include "FreeRTOS.h"
#include "log.h"
#include "task.h"
#include "semphr.h"

#define LVGL_TASK_STACK_SIZE  1024 /* 以 word 为单位（xTaskCreate 栈深度），在 Cortex-M4 上约等于 4KB */
#ifndef LVGL_HANDLER_TASK_PRIORITY
#define LVGL_HANDLER_TASK_PRIORITY (configMAX_PRIORITIES - 3)
#endif

#ifndef LVGL_TICK_TASK_PRIORITY
#define LVGL_TICK_TASK_PRIORITY (configMAX_PRIORITIES - 2)
#endif

#define LVGL_TICK_PERIOD_MS   5

static TaskHandle_t lvgl_task_handle = NULL;
static SemaphoreHandle_t lvgl_mutex = NULL;

/* LVGL 集成模型
 * - 只有 `lvgl_handler_task` 在循环中直接调用 LVGL API。
 * - 其他任务不要直接操作 LVGL 对象；请用 `lv_async_call()` 把 UI 更新投递回 LVGL 线程，
 *   或使用 `lvgl_lock()`/`lvgl_unlock()` 做互斥保护。
 *
 * 优先级说明：
 * - `lvgl_tick_task` 负责 LVGL 的时间基准（动画/输入设备处理/定时器）。
 * - `lvgl_handler_task` 频繁调用 `lv_timer_handler()`；建议较高优先级，避免 CPU 忙时触摸无响应。
 */
static void lvgl_tick_task(void *arg) {
    (void) arg;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(LVGL_TICK_PERIOD_MS);

    while (1) {
        lv_tick_inc(LVGL_TICK_PERIOD_MS);
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

static void lvgl_handler_task(void *arg) {
    (void) arg;

    lv_init();
    LOG_I("LVGL", "[LVGL] LVGL initialized");
    lv_port_disp_init();
    LOG_I("LVGL", "[LVGL] Display driver initialized");
    lv_port_indev_init();
    LOG_I("LVGL", "[LVGL] Input driver initialized");
    ui_lock_init();
    LOG_I("LVGL", "[LVGL] UI initialized");

    while (1) {
        if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            lv_timer_handler();
            xSemaphoreGive(lvgl_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void lvgl_init(void) {
    lvgl_mutex = xSemaphoreCreateMutex();
    if (lvgl_mutex == NULL) {
        LOG_I("LVGL", "[LVGL] xSemaphoreCreateMutex failed, free=%lu minEver=%lu\r\n",
              (unsigned long)xPortGetFreeHeapSize(),
              (unsigned long)xPortGetMinimumEverFreeHeapSize());
        return;
    }

    if (xTaskCreate(lvgl_handler_task, "lvgl_handler", LVGL_TASK_STACK_SIZE, NULL, LVGL_HANDLER_TASK_PRIORITY,
                    &lvgl_task_handle) != pdPASS) {
        LOG_I("LVGL", "[LVGL] lvgl_handler task create failed, free=%lu minEver=%lu\r\n",
              (unsigned long)xPortGetFreeHeapSize(),
              (unsigned long)xPortGetMinimumEverFreeHeapSize());
        return;
    }

    if (xTaskCreate(lvgl_tick_task, "lvgl_tick", configMINIMAL_STACK_SIZE, NULL, LVGL_TICK_TASK_PRIORITY, NULL) !=
        pdPASS) {
        LOG_I("LVGL", "[LVGL] lvgl_tick task create failed, free=%lu minEver=%lu\r\n",
              (unsigned long)xPortGetFreeHeapSize(),
              (unsigned long)xPortGetMinimumEverFreeHeapSize());
        return;
    }
}

void lvgl_lock(void) {
    if (lvgl_mutex != NULL) {
        xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
    }
}

void lvgl_unlock(void) {
    if (lvgl_mutex != NULL) {
        xSemaphoreGive(lvgl_mutex);
    }
}

