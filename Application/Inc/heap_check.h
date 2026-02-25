#ifndef SMARTLOCK_HEAP_CHECK_H
#define SMARTLOCK_HEAP_CHECK_H
#include "task.h"
#include "cmsis_os2.h"
void vHEAP_check_task(void *argument);
uint32_t resource_mon_cpu_load_pct(void);
uint32_t resource_mon_stack_used_pct(TaskHandle_t h, uint32_t stack_bytes);
uint32_t resource_mon_stack_min_free_bytes(TaskHandle_t h);



extern osThreadId_t KeyScanTaskHandle;
extern osThreadId_t uartTaskHandle;
extern osThreadId_t lcdTaskHandle;
extern osThreadId_t LightSensor_TaskHandle;
extern osThreadId_t TouchTest_TaskHandle;
extern osThreadId_t heap_check_task_handle;

/* 注意：假设这两个是你自己在其他 C 文件里定义的变量，这里 extern 是没问题的 */
extern osal_thread_t AT_Core_Task_Handle;
extern osal_thread_t s_logTaskHandle;
#endif  // SMARTLOCK_HEAP_CHECK_H
