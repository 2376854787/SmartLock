#include "heap_check.h"

#include <string.h>

#include "log.h"
#include "osal.h"
#include "watchdog_app.h"
#include "wdg_supervisor.h"

#define RES_MON_MAX_TASKS 24u

static uint32_t s_res_prev_total_runtime = 0u;
static uint32_t s_res_prev_idle_runtime  = 0u;
static uint8_t s_res_runtime_inited      = 0u;
void vHEAP_check_task(void* argument) {
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

        LOG_D("stack", "当前任务:%s 最低水位: %lu Bytes", osThreadGetName(eventBusTaskHandle),
              (unsigned long)uxTaskGetStackHighWaterMark((TaskHandle_t)eventBusTaskHandle) * 4);

        /* 动态获取 LVGL 渲染线程的句柄并打印 */
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
/**
 * @brief cpu负载计算
 * @return 返回系统占用百分比
 */
uint32_t resource_mon_cpu_load_pct(void) {
    /* 存储所有任务的状态 */
    TaskStatus_t task_stats[RES_MON_MAX_TASKS];
    uint32_t total_runtime = 0u;
    const UBaseType_t n =
        uxTaskGetSystemState(task_stats, (UBaseType_t)RES_MON_MAX_TASKS, &total_runtime);
    if ((n == 0u) || (total_runtime == 0u)) return 0u;

    uint32_t idle_runtime = 0u;
    /* 从系统状态中找到 IDLE 任务的 运行总时间片 */
    for (UBaseType_t i = 0u; i < n; ++i) {
        const char* name = task_stats[i].pcTaskName;
        if ((name != NULL) && (strncmp(name, "IDLE", 4u) == 0)) {
            idle_runtime += task_stats[i].ulRunTimeCounter;
        }
    }
    /* 初始化一次 */
    if (s_res_runtime_inited == 0u) {
        /* 保存系统总运行时间 */
        s_res_prev_total_runtime = total_runtime;
        /* 保存IDLE 空闲任务总时间 */
        s_res_prev_idle_runtime  = idle_runtime;
        s_res_runtime_inited     = 1u;
        return 0u;
    }
    /* 进行差分计算 */
    const uint32_t delta_total = total_runtime - s_res_prev_total_runtime;
    const uint32_t delta_idle  = idle_runtime - s_res_prev_idle_runtime;
    s_res_prev_total_runtime   = total_runtime;
    s_res_prev_idle_runtime    = idle_runtime;

    if ((delta_total == 0u) || (delta_idle > delta_total)) return 0u;
    /* 计算系统占用百分比 */
    const uint32_t idle_pct = (uint32_t)(((uint64_t)delta_idle * 100u) / (uint64_t)delta_total);
    return (idle_pct >= 100u) ? 0u : (100u - idle_pct);
}
/**
 * @brief 进行某个任务的最低水位线百分比计算
 * @param h 任务句柄
 * @param stack_bytes 任务分配的栈大小
 * @return 最小水位线 的百分比字节数
 */
uint32_t resource_mon_stack_used_pct(TaskHandle_t h, uint32_t stack_bytes) {
    if ((h == NULL) || (stack_bytes == 0u)) return 0u;
    /* 获取水位线 */
    const uint32_t min_free_bytes =
        (uint32_t)uxTaskGetStackHighWaterMark(h) * (uint32_t)sizeof(StackType_t);
    if (min_free_bytes >= stack_bytes) return 0u;
    /* 返回百分比 */
    return (uint32_t)(((uint64_t)(stack_bytes - min_free_bytes) * 100u) / (uint64_t)stack_bytes);
}
/**
 * @brief 获取某个任务的最低水位线字节数
 * @param h 任务句柄
 * @return 最低字节数
 */
uint32_t resource_mon_stack_min_free_bytes(TaskHandle_t h) {
    if (h == NULL) return 0u;
    return (uint32_t)uxTaskGetStackHighWaterMark(h) * (uint32_t)sizeof(StackType_t);
}