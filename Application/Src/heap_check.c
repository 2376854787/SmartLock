#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "log.h"
#include "osal.h"
#include "task.h"
extern osThreadId_t KeyScanTaskHandle;
extern osThreadId_t uartTaskHandle;
extern osThreadId_t lcdTaskHandle;
extern osThreadId_t LightSensor_TaskHandle;
extern osThreadId_t TouchTest_TaskHandle;
extern osThreadId_t heap_check_task_handle;
extern osal_thread_t AT_Core_Task_Handle;
extern osal_thread_t s_logTaskHandle;

void vHEAP_check_task(void *argument) {
    for (;;) {
        LOG_D("heap", "全局最低剩余堆大小: %d", xPortGetMinimumEverFreeHeapSize());
        osDelay(1000);
        LOG_D("stack", "当前任务:%s最低水位:  %lu", osThreadGetName(KeyScanTaskHandle),
             (unsigned long)uxTaskGetStackHighWaterMark(KeyScanTaskHandle));
        LOG_D("stack", "当前任务:%s最低水位:  %lu", osThreadGetName(uartTaskHandle),
              (unsigned long)uxTaskGetStackHighWaterMark(uartTaskHandle));
        LOG_D("stack", "当前任务:%s最低水位:  %lu", osThreadGetName(lcdTaskHandle),
              (unsigned long)uxTaskGetStackHighWaterMark(lcdTaskHandle));
        LOG_D("stack", "当前任务:%s最低水位:  %lu", osThreadGetName(LightSensor_TaskHandle),
              (unsigned long)uxTaskGetStackHighWaterMark(LightSensor_TaskHandle));
        // LOG_D("stack", "当前任务:%s最低水位:  %lu", osThreadGetName(TouchTest_TaskHandle),
        //       (unsigned long)uxTaskGetStackHighWaterMark(TouchTest_TaskHandle));
        LOG_D("stack", "当前任务:%s最低水位:  %lu", osThreadGetName(heap_check_task_handle),
              (unsigned long)uxTaskGetStackHighWaterMark(heap_check_task_handle));
        LOG_D("stack", "当前任务:%s最低水位:  %lu", osThreadGetName(AT_Core_Task_Handle),
              (unsigned long)uxTaskGetStackHighWaterMark(AT_Core_Task_Handle));
        LOG_D("stack", "当前任务:%s最低水位:  %lu", osThreadGetName(s_logTaskHandle),
              (unsigned long)uxTaskGetStackHighWaterMark(s_logTaskHandle));
    }
}
