#include "../Inc/LCD_task.h"

#include "AT_Core_Task.h"
#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "heap_check.h"
#include "src/stdlib/lv_sprintf.h"
#include "src/widgets/label/lv_label.h"
#include "task.h"

#define STACK_BYTES_KEYSCAN_TASK      (300u * 4u)
#define STACK_BYTES_UART_TASK         (256u * 4u)
#define STACK_BYTES_LCD_TASK          (1024u * 4u)
#define STACK_BYTES_LIGHT_SENSOR_TASK (128u * 4u)
#define STACK_BYTES_HEAP_CHECK_TASK   (256u * 6u)
#define STACK_BYTES_AT_CORE_TASK      (256u * 6u)
#define STACK_BYTES_EVENT_BUS_TASK    (256u * 2u)

static TaskHandle_t s_lvgl_swdraw_handle = NULL;
/**
 * @brief 更新lvgl资源表的显示
 * @param label lvgl 资源
 */
void resource_mon_update_label(lv_obj_t* label) {
    if (label == NULL) return;

    if (s_lvgl_swdraw_handle == NULL) {
        /* 获取到绘制任务句柄 */
        s_lvgl_swdraw_handle = xTaskGetHandle("swdraw");
    }
    /* 获取系统需要的信息 */
    /* cpu 占用率 */
    const uint32_t cpu_pct      = resource_mon_cpu_load_pct();
    /* 总堆大小 */
    const uint32_t total_heap   = (uint32_t)configTOTAL_HEAP_SIZE;
    /* 剩余堆大小 */
    const uint32_t free_heap    = xPortGetFreeHeapSize();
    /* 最小堆大小 */
    const uint32_t min_freeheap = xPortGetMinimumEverFreeHeapSize();
    const uint32_t used_heap    = (free_heap <= total_heap) ? (total_heap - free_heap) : 0u;
    /* 系统堆使用百分比 */
    const uint32_t heap_pct =
        (total_heap != 0u) ? (uint32_t)(((uint64_t)used_heap * 100u) / (uint64_t)total_heap) : 0u;
    /* 计算任务栈的最低水位线 百分比 */
    const uint32_t key_pct =
        resource_mon_stack_used_pct((TaskHandle_t)KeyScanTaskHandle, STACK_BYTES_KEYSCAN_TASK);
    const uint32_t uart_pct =
        resource_mon_stack_used_pct((TaskHandle_t)uartTaskHandle, STACK_BYTES_UART_TASK);
    const uint32_t lcd_pct =
        resource_mon_stack_used_pct((TaskHandle_t)lcdTaskHandle, STACK_BYTES_LCD_TASK);
    const uint32_t light_pct  = resource_mon_stack_used_pct((TaskHandle_t)LightSensor_TaskHandle,
                                                            STACK_BYTES_LIGHT_SENSOR_TASK);
    const uint32_t heapck_pct = resource_mon_stack_used_pct((TaskHandle_t)heap_check_task_handle,
                                                            STACK_BYTES_HEAP_CHECK_TASK);
    const uint32_t at_pct =
        resource_mon_stack_used_pct((TaskHandle_t)g_at_manager.core_task, STACK_BYTES_AT_CORE_TASK);
    const uint32_t event_bus_pct =
        resource_mon_stack_used_pct((TaskHandle_t)eventBusTaskHandle, STACK_BYTES_EVENT_BUS_TASK);
    const uint32_t swdraw_wm = resource_mon_stack_min_free_bytes(s_lvgl_swdraw_handle);

    char panel[320];
    lv_snprintf(
        panel, sizeof(panel),
        "CPU:%3lu%%\nHeap:%5lu/%5luB(%2lu%%)\nMinHeap:%5luB\n"
        "Stk K/U/L:%2lu/%2lu/%2lu%%\nStk S/H/A:%2lu/%2lu/%2lu%%\neventbus: %2lu%%\nSWD min:%4luB",
        (unsigned long)cpu_pct, (unsigned long)used_heap, (unsigned long)total_heap,
        (unsigned long)heap_pct, (unsigned long)min_freeheap, (unsigned long)key_pct,
        (unsigned long)uart_pct, (unsigned long)lcd_pct, (unsigned long)light_pct,
        (unsigned long)heapck_pct, (unsigned long)at_pct, (unsigned long)event_bus_pct,
        (unsigned long)swdraw_wm);
    lv_label_set_text(label, panel);
}