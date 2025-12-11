/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "RingBuffer.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

#include "ESP01S.h"
#include "KEY.h"
#include "lcd.h"
#include "Light_Sensor_task.h"
#include "log.h"
#include "MyPrintf.h"
#include "tim.h"
#include "wifi_mqtt_task.h"
#include "mqtt_at_task.h"
#include "Usart1_manage.h"
#include "water_adc.h"
#include "AT.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */



/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */


/* USER CODE END Variables */
/* Definitions for KeyScanTask */
osThreadId_t KeyScanTaskHandle;
const osThreadAttr_t KeyScanTask_attributes = {
  .name = "KeyScanTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for uartTask */
osThreadId_t uartTaskHandle;
const osThreadAttr_t uartTask_attributes = {
  .name = "uartTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for lcdTask */
osThreadId_t lcdTaskHandle;
const osThreadAttr_t lcdTask_attributes = {
  .name = "lcdTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityLow,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* ESP01s MQTT 任务 */
osThreadId_t MqttTaskHandle;
const osThreadAttr_t MqttTask_attributes = {
    .name = "MqttTask",
    .stack_size = 1024 * 4, // MQTT任务需要更大的栈
    .priority = (osPriority_t) osPriorityNormal,
};
osThreadId_t LightSensor_TaskHandle;
/* 光敏传感器任务 */
const osThreadAttr_t LightSensor_Task_attributes = {
    .name = "LightSensor_Task",
    .stack_size = 256 * 4,
    .priority = (osPriority_t) osPriorityNormal,
};

/* 水滴传感器ADC采集 逻辑处理任务 */
osThreadId_t Water_Sensor_TaskHandle;
const osThreadAttr_t Water_Sensor_attributes = {
    .name = "Water_Sensor_Task",
    .stack_size = 256 * 4,
    .priority = (osPriority_t) osPriorityNormal,
};
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void StartTask02(void *argument);
void StartTask_LCD(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void configureTimerForRunTimeStats(void);
unsigned long getRunTimeCounterValue(void);
void vApplicationIdleHook(void);
void vApplicationTickHook(void);
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);
void vApplicationMallocFailedHook(void);

/* USER CODE BEGIN 1 */
/* Functions needed when configGENERATE_RUN_TIME_STATS is on */
__weak void configureTimerForRunTimeStats(void) {
}

__weak unsigned long getRunTimeCounterValue(void) {
    return 0;
}

/* USER CODE END 1 */

/* USER CODE BEGIN 2 */
void vApplicationIdleHook(void) {
    /* vApplicationIdleHook() will only be called if configUSE_IDLE_HOOK is set
    to 1 in FreeRTOSConfig.h. It will be called on each iteration of the idle
    task. It is essential that code added to this hook function never attempts
    to block in any way (for example, call xQueueReceive() with a block time
    specified, or call vTaskDelay()). If the application makes use of the
    vTaskDelete() API function (as this demo application does) then it is also
    important that vApplicationIdleHook() is permitted to return to its calling
    function, because it is the responsibility of the idle task to clean up
    memory allocated by the kernel to any task that has since been deleted. */
}

/* USER CODE END 2 */

/* USER CODE BEGIN 3 */
void vApplicationTickHook(void) {
    /* This function will be called by each tick interrupt if
    configUSE_TICK_HOOK is set to 1 in FreeRTOSConfig.h. User code can be
    added here, but the tick hook is called from an interrupt context, so
    code must not attempt to block, and only the interrupt safe FreeRTOS API
    functions can be used (those that end in FromISR()). */
}

/* USER CODE END 3 */

/* USER CODE BEGIN 4 */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName) {
    printf("Stack overflow in task: %s\r\n", pcTaskName);
    taskDISABLE_INTERRUPTS();
    for (;;) {
    }
}

/* USER CODE END 4 */

/* USER CODE BEGIN 5 */
void vApplicationMallocFailedHook(void) {
    /* vApplicationMallocFailedHook() will only be called if
    configUSE_MALLOC_FAILED_HOOK is set to 1 in FreeRTOSConfig.h. It is a hook
    function that will get called if a call to pvPortMalloc() fails.
    pvPortMalloc() is called internally by the kernel whenever a task, queue,
    timer or semaphore is created. It is also called by various parts of the
    demo application. If heap_1.c or heap_2.c are used, then the size of the
    heap available to pvPortMalloc() is defined by configTOTAL_HEAP_SIZE in
    FreeRTOSConfig.h, and the xPortGetFreeHeapSize() API function can be used
    to query the size of free heap space that remains (although it does not
    provide information on how the remaining heap might be fragmented). */
}

/* USER CODE END 5 */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
    /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
    /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
    /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
    /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of KeyScanTask */
  KeyScanTaskHandle = osThreadNew(StartDefaultTask, NULL, &KeyScanTask_attributes);

  /* creation of uartTask */
  uartTaskHandle = osThreadNew(StartTask02, NULL, &uartTask_attributes);

  /* creation of lcdTask */
  lcdTaskHandle = osThreadNew(StartTask_LCD, NULL, &lcdTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
    /* add threads, ... */

    /* 光敏传感器 */
    LightSensor_TaskHandle = osThreadNew(StartLightSensorTask, NULL, &LightSensor_Task_attributes);
    /* ESP01s */
    // MqttTaskHandle = osThreadNew(StartMqttAtTask, NULL, &MqttTask_attributes);

    /* 水滴传感器 任务*/
    Water_Sensor_TaskHandle = osThreadNew(waterSensor_task, NULL, &Water_Sensor_attributes);
    /* 日志任务 创建信号量、创建任务 */
    Log_Init();


  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
    /* add events, ... */

  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
    /* Infinite loop */
    for (;;) {
        //LED 1翻转
        HAL_GPIO_TogglePin(LED1_GPIO_Port,LED1_Pin);
        KEY_Tasks();
        //UBaseType_t watermark = uxTaskGetStackHighWaterMark(NULL);
        // printf("keyscanTask high watermark = %lu\r\n", (unsigned long) watermark);
        osDelay(10);
    }
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_StartTask02 */
/**
* @brief Function implementing the uartTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask02 */
void StartTask02(void *argument)
{
  /* USER CODE BEGIN StartTask02 */
    char buffer[128];
    /* Infinite loop */
    for (;;) {
        uint16_t read_size = RingBuffer_GetUsedSize(&g_rb_uart1);
        if (read_size > 0) {
            if (read_size > 127) read_size = 127;
            if (ReadRingBuffer(&g_rb_uart1, (uint8_t *) buffer, &read_size, 0)) {
                buffer[read_size] = '\0';
                // printf("%s\n", buffer);
                HAL_UART_Transmit(&huart3, (const uint8_t *) buffer, strlen((char *) buffer), HAL_MAX_DELAY);
            } else {
                printf("读取失败\n");
            }
        }

        HAL_GPIO_TogglePin(LED0_GPIO_Port,LED0_Pin);
        osDelay(1000);
    }
  /* USER CODE END StartTask02 */
}

/* USER CODE BEGIN Header_StartTask_LCD */
/**
* @brief Function implementing the lcdTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask_LCD */
void StartTask_LCD(void *argument)
{
  /* USER CODE BEGIN StartTask_LCD */
    /* Infinite loop */
    char buffer[128];
    osDelay(2000);
    //esp01s_Init(&huart3, 1024);
    LOG_I("StartTask_LCD", "启动完成");
    LOG_I("111", "启动完成");
    for (;;) {
        snprintf(buffer, 128, "Time:%lu", HAL_GetTick());
        lcd_show_string(50, 300, 240, 32, 32, buffer, BLACK);

       // UBaseType_t watermark = uxTaskGetStackHighWaterMark(NULL);
        // printf("lcdTask high watermark = %lu\r\n", (unsigned long) watermark);
        osDelay(20);
    }
  /* USER CODE END StartTask_LCD */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */


void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
    if (huart->Instance == USART1) {
        // 串口1任务 维护一个指针在IDLE 以及半满全满中断中处理
        process_dma_data();
        return;
    }

    if (huart->Instance == USART3) {
        AT_Core_RxCallback(huart, Size);
        if (Size > 0) {
        }

        /* 1、获取接收到的数据写入rb */


        /* 2、通知任务 暂时用同步代码 待实现任务事件列表*/
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        //uint32_t error = HAL_UART_GetError(huart);
        // 处理错误，如 ORE/FE
        __HAL_UART_CLEAR_OREFLAG(huart);
        __HAL_UART_CLEAR_FEFLAG(huart);
        // 重启 UART 和 DMA
        HAL_UART_DMAStop(huart);
        MX_USART1_UART_Init();
        HAL_UART_Receive_DMA(huart, DmaBuffer, DMA_BUFFER_SIZE);
    }
}


/* USER CODE END Application */

