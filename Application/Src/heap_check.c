#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "log.h"
#include "osal.h"
#include "task.h"
#include "watchdog_app.h"
#include "wdg_supervisor.h"

extern osThreadId_t KeyScanTaskHandle;
extern osThreadId_t uartTaskHandle;
extern osThreadId_t lcdTaskHandle;
extern osThreadId_t LightSensor_TaskHandle;
extern osThreadId_t TouchTest_TaskHandle;
extern osThreadId_t heap_check_task_handle;

/* 注意：假设这两个是你自己在其他 C 文件里定义的变量，这里 extern 是没问题的 */
extern osal_thread_t AT_Core_Task_Handle;
extern osal_thread_t s_logTaskHandle;

void vHEAP_check_task(void *argument) {
    uint8_t id = 0;
    wdg_sup_register(&id, "heap check task", WDG_WATCH_CHALLENGE, WDG_ALGO_MATH_MIX32, 2, 3,
                     2 * 1000 + CFG_PARAM_WATCHDOG_APP_SUP_PERIOD_MS + 50);

    /* 定义一个句柄专门用来存 LVGL 渲染线程 */
    TaskHandle_t lvgl_draw_task_handle = NULL;

    for (;;) {
        /* 打印全局堆剩余 (xPortGetMinimumEverFreeHeapSize 返回的是 Byte) */
        LOG_D("heap", "全局最低剩余堆大小: %d Bytes", xPortGetMinimumEverFreeHeapSize());
        osDelay(1000);

        /* 打印普通任务的栈剩余 (注意 *4 换算成 Byte) */
        LOG_D("stack", "当前任务:%s 最低水位: %lu Bytes", osThreadGetName(KeyScanTaskHandle),
              (unsigned long)uxTaskGetStackHighWaterMark((TaskHandle_t)KeyScanTaskHandle) * 4);

        LOG_D("stack", "当前任务:%s 最低水位: %lu Bytes", osThreadGetName(uartTaskHandle),
              (unsigned long)uxTaskGetStackHighWaterMark((TaskHandle_t)uartTaskHandle) * 4);

        LOG_D("stack", "当前任务:%s 最低水位: %lu Bytes", osThreadGetName(lcdTaskHandle),
              (unsigned long)uxTaskGetStackHighWaterMark((TaskHandle_t)lcdTaskHandle) * 4);

        LOG_D("stack", "当前任务:%s 最低水位: %lu Bytes", osThreadGetName(LightSensor_TaskHandle),
              (unsigned long)uxTaskGetStackHighWaterMark((TaskHandle_t)LightSensor_TaskHandle) * 4);

        LOG_D("stack", "当前任务:%s 最低水位: %lu Bytes", osThreadGetName(heap_check_task_handle),
              (unsigned long)uxTaskGetStackHighWaterMark((TaskHandle_t)heap_check_task_handle) * 4);

        /* 【关键修复】：动态获取 LVGL 渲染线程的句柄并打印 */
        if (lvgl_draw_task_handle == NULL) {
            /* 只在找不到的时候去查名字，查到了就缓存起来，节省 CPU 资源 */
            lvgl_draw_task_handle = xTaskGetHandle("swdraw");
        }

        if (lvgl_draw_task_handle != NULL) {
            LOG_D("stack", "当前任务:swdraw 最低水位: %lu Bytes",
                  (unsigned long)uxTaskGetStackHighWaterMark(lvgl_draw_task_handle) * 4);
        } else {
            LOG_D("stack", "LVGL 渲染线程(swdraw)尚未创建或名字不匹配");
        }

        /* 喂狗 */
        wdg_sup_task_service(id);

        OSAL_delay_ms(1000);
    }
}