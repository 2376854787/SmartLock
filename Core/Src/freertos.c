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
#include <stdio.h>
#include <string.h>

#include "AT.h"
#include "AT_Core_Task.h"
#include "ESP01S.h"
#include "KEY.h"
#include "Light_Sensor_task.h"
#include "RingBuffer.h"
#include "Usart1_manage.h"
#include "bh1750.h"
#include "crc16.h"
#include "lcd.h"
#include "log.h"
#include "log_port.h"
#include "mqtt_at_task.h"
#include "ota_http.h"
#include "ota_manager.h"
#include "ota_version.h"
#include "ret_code.h"
#include "tim.h"
#include "usart.h"
#include "water_adc.h"
#include "wifi_mqtt_task.h"

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
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for uartTask */
osThreadId_t uartTaskHandle;
const osThreadAttr_t uartTask_attributes = {
  .name = "uartTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for lcdTask */
osThreadId_t lcdTaskHandle;
const osThreadAttr_t lcdTask_attributes = {
  .name = "lcdTask",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityLow,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* ESP01s MQTT 任务 */
osThreadId_t MqttTaskHandle;
const osThreadAttr_t MqttTask_attributes = {
    .name       = "MqttTask",
    .stack_size = 1024 * 4,  // MQTT任务需要更大的栈
    .priority   = (osPriority_t)osPriorityNormal,
};
osThreadId_t LightSensor_TaskHandle;
/* 光敏传感器任务 */
const osThreadAttr_t LightSensor_Task_attributes = {
    .name       = "LightSensor_Task",
    .stack_size = 256 * 6,
    .priority   = (osPriority_t)osPriorityNormal,
};

/* 水滴传感器ADC采集 逻辑处理任务 */
osThreadId_t Water_Sensor_TaskHandle;
const osThreadAttr_t Water_Sensor_attributes = {
    .name       = "Water_Sensor_Task",
    .stack_size = 256 * 4,
    .priority   = (osPriority_t)osPriorityNormal,
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
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char* pcTaskName) {
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
    LightSensor_TaskHandle  = osThreadNew(StartLightSensorTask, NULL, &LightSensor_Task_attributes);
    /* ESP01s */
    // MqttTaskHandle = osThreadNew(StartMqttAtTask, NULL, &MqttTask_attributes);

    /* 水滴传感器 任务*/
    Water_Sensor_TaskHandle = osThreadNew(waterSensor_task, NULL, &Water_Sensor_attributes);
    /* 日志任务 创建信号量、创建任务 */
    Log_PortInit();
    Log_Init();
    /* 串口AT解析任务 创建信号量、创建任务*/
    at_core_task_init(&g_at_manager, &huart3);

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
        // LED 1翻转
        HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin);
        KEY_Tasks();
        // UBaseType_t watermark = uxTaskGetStackHighWaterMark(NULL);
        //  printf("keyscanTask high watermark = %lu\r\n", (unsigned long) watermark);
        const float lx       = BH1750_Get_LX();
        const uint32_t prase = (uint32_t)(lx * 100);
        // LOG_D("光照度", "环境光lx：%ld.%02ld\r\n", prase / 100, prase % 100);
        char buffer[64];
        sprintf(buffer, "%ld", prase);
        lcd_show_string(10, 400, 240, 32, 32, buffer, RED);
        osDelay(30);
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
        uint32_t read_size = RingBuffer_GetUsedSize(&g_rb_uart1);
        if (read_size > 0) {
            if (read_size > 127) read_size = 127;
            if (ReadRingBuffer(&g_rb_uart1, (uint8_t*)buffer, &read_size, 0) == RET_OK) {
                buffer[read_size] = '\0';

                /* 检查是否收到 OTA 升级命令 */
                if (strstr(buffer, "OTA") != NULL) {
                    LOG_I("OTA_CMD", "Received OTA command, triggering upgrade...");

                    /* 写入 MCUBoot Magic 到 Slot 1 末尾 */
                    if (ota_manager_finish() == RET_OK) {
                        LOG_I("OTA_CMD", "Magic written! Rebooting in 1s...");
                        osDelay(1000);
                        NVIC_SystemReset();
                    } else {
                        LOG_E("OTA_CMD", "Failed to write magic!");
                    }
                }

                /* 检查是否收到 CONFIRM 命令（手动确认升级成功）*/
                if (strstr(buffer, "CONFIRM") != NULL) {
                    LOG_I("OTA_CONFIRM", "=== OTA Upgrade Confirmed! ===");
                    LOG_I("OTA_CONFIRM", "New firmware is running successfully.");
                    ota_manager_confirm_upgrade();
                }

                /* 透传命令：以 "AT:" 开头的命令直接发送给 ESP01S
                 * 例如：AT:AT+CIFSR 会发送 "AT+CIFSR\r\n" 给 ESP01S
                 */
                if (strncmp(buffer, "AT:", 3) == 0) {
                    char at_cmd[128];
                    snprintf(at_cmd, sizeof(at_cmd), "%s\r\n", buffer + 3);
                    LOG_I("AT_PASSTHROUGH", "Sending: %s", at_cmd);
                    AT_Resp_t resp = AT_SendCmd(&g_at_manager, at_cmd, "OK", 5000);
                    LOG_I("AT_PASSTHROUGH", "Response: %d", resp);
                }

                /* 波特率切换命令：BAUD:921600 */
                if (strncmp(buffer, "BAUD:", 5) == 0) {
                    unsigned int baud = 0;
                    if (sscanf(buffer + 5, "%u", &baud) == 1) {
                        LOG_I("CMD", "Request to switch baudrate to %u", baud);
                        if (AT_SwitchBaudrate(&g_at_manager, (uint32_t)baud)) {
                            LOG_I("CMD", "Switch Success!");
                        } else {
                            LOG_E("CMD", "Switch Failed!");
                        }
                    }
                }
            }
        }

        HAL_GPIO_TogglePin(LED0_GPIO_Port, LED0_Pin);
        osDelay(100);
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
    esp01s_Init(&huart3, 1024);
    extern volatile bool g_trigger_ota;
    LOG_I("StartTask_LCD", "启动完成");
    LOG_I("111", "启动完成");

    /* 显示固件版本号 (从 MCUBoot 镜像头读取) */
    mcuboot_version_t fw_ver;
    char ver_str[32];
    if (ota_get_running_version(&fw_ver) == 0) {
        ota_version_to_string(&fw_ver, ver_str, sizeof(ver_str));
    } else {
        snprintf(ver_str, sizeof(ver_str), "v?.?.?");  // 无有效头
    }
    char ver_display[48];
    snprintf(ver_display, sizeof(ver_display), "FW: %s", ver_str);
    lcd_show_string(10, 10, 240, 24, 24, ver_display, BLUE);

    for (;;) {
        if (g_trigger_ota) {
            g_trigger_ota = false;
            LOG_I("LCDTask", "检测到 OTA 请求，开始执行...");

            ota_http_config_t config = {.server_ip   = "192.168.31.223",  // 请修改为实际 IP
                                        .server_port = 8000,
                                        .url_path    = "/123.bin"};
            if (ota_http_download(&config) == RET_OK) {
                LOG_I("OTA", "Success! Rebooting...");
                HAL_Delay(1000);
                NVIC_SystemReset();
            } else {
                LOG_E("OTA", "Failed!");
            }
        }

        sniprintf(buffer, 128, "Time:%lu", HAL_GetTick());
        lcd_show_string(50, 300, 240, 32, 32, buffer, BLACK);

        // UBaseType_t watermark = uxTaskGetStackHighWaterMark(NULL);
        // printf("lcdTask high watermark = %lu\r\n", (unsigned long) watermark);
        osDelay(20);
    }
  /* USER CODE END StartTask_LCD */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* huart, uint16_t Size) {
    if (huart->Instance == USART1) {
        // 串口1任务 维护一个指针在IDLE 以及半满全满中断中处理
        process_dma_data();
        return;
    }

    if (huart->Instance == USART3) {
        AT_Core_RxCallback(&g_at_manager, &huart3, Size);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef* huart) {
    if (huart->Instance == USART1) {
        // uint32_t error = HAL_UART_GetError(huart);
        //  处理错误，如 ORE/FE
        __HAL_UART_CLEAR_OREFLAG(huart);
        __HAL_UART_CLEAR_FEFLAG(huart);
        // 重启 UART 和 DMA
        HAL_UART_DMAStop(huart);
        MX_USART1_UART_Init();
        HAL_UART_Receive_DMA(huart, DmaBuffer, DMA_BUFFER_SIZE);
    }
}

/* USER CODE END Application */

