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

#include "cmsis_os.h"
#include "main.h"
#include "task.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>

#include "AT_Core_Task.h"
#include "ESP01S.h"
#include "KEY.h"
#include "LCD_task.h"
#include "Light_Sensor_task.h"
#include "bh1750.h"
#include "crc16.h"
#include "hal_time.h"
#include "hal_uart_port_hooks.h"
#include "heap_check.h"
#include "log.h"
#include "log_port.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "lvgl.h"
#include "watchdog_app.h"
#include "wdg_supervisor.h"
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
    .name       = "KeyScanTask",
    .stack_size = 300 * 4,
    .priority   = (osPriority_t)osPriorityNormal,
};
/* Definitions for uartTask */
osThreadId_t uartTaskHandle;
const osThreadAttr_t uartTask_attributes = {
    .name       = "uartTask",
    .stack_size = 256 * 4,
    .priority   = (osPriority_t)osPriorityLow,
};
/* Definitions for lcdTask */
osThreadId_t lcdTaskHandle;
const osThreadAttr_t lcdTask_attributes = {
    .name       = "lcdTask",
    .stack_size = 1024 * 4,
    .priority   = (osPriority_t)osPriorityLow,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

osThreadId_t LightSensor_TaskHandle;
/* 光敏传感器任务 */
const osThreadAttr_t LightSensor_Task_attributes = {
    .name       = "LightSensor_Task",
    .stack_size = 128 * 4,
    .priority   = (osPriority_t)osPriorityNormal,
};

/* GT911 触摸测试任务 */
// osThreadId_t TouchTest_TaskHandle;
// const osThreadAttr_t TouchTest_Task_attributes = {
//     .name       = "TouchTestTask",
//     .stack_size = 512 * 3,
//     .priority   = (osPriority_t)osPriorityNormal,
// };

osThreadId_t heap_check_task_handle;
const osThreadAttr_t heap_check_task_attributes = {
    .name       = "heap_check_task",
    .stack_size = 256 * 6,
    .priority   = (osPriority_t)osPriorityLow,
};
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void* argument);
void StartTask02(void* argument);
void StartTask_LCD(void* argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void configureTimerForRunTimeStats(void);
unsigned long getRunTimeCounterValue(void);
void vApplicationIdleHook(void);
void vApplicationTickHook(void);
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char* pcTaskName);
void vApplicationMallocFailedHook(void);

/* USER CODE BEGIN 1 */
/* Functions needed when configGENERATE_RUN_TIME_STATS is on */
__weak void configureTimerForRunTimeStats(void) {
    /* Keep CYCCNT monotonic: only enable, do not reset counter. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

__weak unsigned long getRunTimeCounterValue(void) {
    return (unsigned long)DWT->CYCCNT;
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
    printf("FreeRTOS malloc failed! Free heap: %u\r\n", (unsigned)xPortGetFreeHeapSize());
    taskDISABLE_INTERRUPTS();
    for (;;) {
    }
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
    KeyScanTaskHandle      = osThreadNew(StartDefaultTask, NULL, &KeyScanTask_attributes);

    /* creation of uartTask */
    uartTaskHandle         = osThreadNew(StartTask02, NULL, &uartTask_attributes);

    /* creation of lcdTask */
    lcdTaskHandle          = osThreadNew(StartTask_LCD, NULL, &lcdTask_attributes);

    /* USER CODE BEGIN RTOS_THREADS */
    /* add threads, ... */

    /* 光敏传感器 */
    LightSensor_TaskHandle = osThreadNew(StartLightSensorTask, NULL, &LightSensor_Task_attributes);

    /* GT911 触摸测试任务 */
    // TouchTest_TaskHandle    = osThreadNew(StartTouchTestTask, NULL, &TouchTest_Task_attributes);
    /* 栈 堆大小水位监测 */
    heap_check_task_handle = osThreadNew(vHEAP_check_task, NULL, &heap_check_task_attributes);
    /* 日志任务 创建信号量、创建任务 */
    Log_PortInit();
    Log_Init();
    /* 串口AT解析任务 创建信号量、创建任务*/
    at_core_task_init(&g_at_manager);
    /* 启用 IWDG 抽象 + Supervisor 线程 */
    if (ret_is_err(Watchdog_AppInit())) {
        Error_Handler();
    }

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
void StartDefaultTask(void* argument) {
    /* USER CODE BEGIN StartDefaultTask */
    /* Infinite loop */
    /* 注册看门狗挑战任务：deadline = 2倍任务周期 + 监督周期抖动 + 计算时间 + 余量 */
    uint8_t id = 0;
    const uint32_t default_task_deadline =
        wdg_sup_deadline_budget_ms(2u * 30u, CFG_PARAM_WATCHDOG_APP_SUP_PERIOD_MS, 0u, 50u);
    wdg_sup_register(&id, "default task", WDG_WATCH_CHALLENGE, WDG_ALGO_MATH_MIX32, 1234, 5,
                     default_task_deadline);
    for (;;) {
        // LED 1翻转
        HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin);
        KEY_Tasks();

        // const float lx       = BH1750_Get_LX();
        // const uint32_t prase = (uint32_t)(lx * 100);
        // LOG_D("光照度", "环境光lx：%ld.%02ld\r\n", prase / 100, prase % 100);
        // char buffer[64];
        // sprintf(buffer, "%ld", prase);
        // lcd_show_string(10, 400, 240, 32, 32, buffer, RED);
        /* 做题挑战 */
        wdg_sup_task_service(id);
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
void StartTask02(void* argument) {
    /* USER CODE BEGIN StartTask02 */
    /* Infinite loop */
    /* UART任务存在阻塞风险，deadline按2倍任务周期预留 */
    uint8_t id = 0;
    const uint32_t uart_task_deadline =
        wdg_sup_deadline_budget_ms(2u * 250u, CFG_PARAM_WATCHDOG_APP_SUP_PERIOD_MS, 0u, 50u);
    wdg_sup_register(&id, "start task02", WDG_WATCH_CHALLENGE, WDG_ALGO_MATH_MIX32, 2, 3,
                     uart_task_deadline);
    for (;;) {
        HAL_GPIO_TogglePin(LED0_GPIO_Port, LED0_Pin);
        wdg_sup_task_service(id);
        osDelay(250);
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
void StartTask_LCD(void* argument) {
    /* USER CODE BEGIN StartTask_LCD */
    /* Infinite loop */
    lv_init();
    lv_tick_set_cb(hal_get_tick_ms);
    lv_port_disp_init();
    lv_port_indev_init();

    /* 获取当前的屏幕活动对象 */
    lv_obj_t* scr = lv_screen_active();
    /* 设备背景色 */
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x10243A), 0);
    /* 设置完全不透明 */
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* 创建一个文本标签 */
    lv_obj_t* label = lv_label_create(scr);
    /* 显示文字 */
    lv_label_set_text(label, "LVGL OK");
    /* 设置颜色 */
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    /* 居中 */
    lv_obj_center(label);

    /* --- 触控追踪指示器 --- */
    /* 红色圆点: 跟随触摸位置 */
    /* 创建基础对象 */
    lv_obj_t* cursor = lv_obj_create(scr);
    /* 设置大小 */
    lv_obj_set_size(cursor, 20, 20);
    /* 设置圆角 */
    lv_obj_set_style_radius(cursor, LV_RADIUS_CIRCLE, 0);
    /* 设置背景色 */
    lv_obj_set_style_bg_color(cursor, lv_color_hex(0xFF0000), 0);
    /* 设置透明度 */
    lv_obj_set_style_bg_opa(cursor, LV_OPA_COVER, 0);
    /* 设置边框 无 */
    lv_obj_set_style_border_width(cursor, 0, 0);
    /* 设置初始隐藏 */
    lv_obj_add_flag(cursor, LV_OBJ_FLAG_HIDDEN); /* 初始隐藏 */

    /* 坐标文本: 左上角显示 */
    lv_obj_t* coord_label = lv_label_create(scr);
    lv_label_set_text(coord_label, "Touch: ---");
    lv_obj_set_style_text_color(coord_label, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(coord_label, &lv_font_montserrat_14, 0);
    /* 对齐到 左上 各10 像素 */
    lv_obj_align(coord_label, LV_ALIGN_TOP_LEFT, 10, 10);

    /* --- 系统资源占有率面板 --- */
    lv_obj_t* res_label = lv_label_create(scr);
    lv_obj_set_width(res_label, 220);
    lv_label_set_long_mode(res_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(res_label, lv_color_hex(0xFFE08A), 0);
    lv_obj_set_style_text_font(res_label, &lv_font_montserrat_14, 0);
    lv_obj_align(res_label, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_label_set_text(res_label, "RES: init...");

    LOG_I("StartTask_LCD", "LVGL init done");
    configureTimerForRunTimeStats();
    /* 每秒 */
    uint32_t last_report_tick = hal_get_tick_ms();
    /* 每 500ms */
    uint32_t last_res_tick    = hal_get_tick_ms();
    /* esp 初始化标记 */
    uint8_t esp_init_done     = 0;

    for (;;) {
        lv_timer_handler();

        /* --- 触控追踪更新 --- */
        /* 获取触控屏设备 */
        const lv_indev_t* indev = lv_indev_get_next(NULL);
        if (indev) {
            lv_point_t p;
            /* 获取触控点 */
            lv_indev_get_point(indev, &p);
            /* 读取触控状态 */
            const lv_indev_state_t state = lv_indev_get_state(indev);
            /* 触摸状态 */
            if (state == LV_INDEV_STATE_PRESSED) {
                /* 设置对象的地址 */
                lv_obj_set_pos(cursor, p.x - 10, p.y - 10);
                /* 取消隐藏 */
                lv_obj_clear_flag(cursor, LV_OBJ_FLAG_HIDDEN);

                char buf[32];
                /* 更新 左上角的 label 文本 */
                lv_snprintf(buf, sizeof(buf), "Touch: %d, %d", p.x, p.y);
                lv_label_set_text(coord_label, buf);
            } else { /* 其它状态 隐藏 对象 */
                lv_obj_add_flag(cursor, LV_OBJ_FLAG_HIDDEN);
            }
        }
        /* 等待屏幕刷新超过 20次 初始化esp */
        if (!esp_init_done && lv_port_disp_get_flush_count() > 20) {
            esp01s_Init();
            esp_init_done = 1;
            LOG_I("StartTask_LCD", "ESP init done");
        }
        /* 每秒更新打印当前任务的栈水位线 */
        if ((hal_get_tick_ms() - last_report_tick) >= 1000U) {
            const UBaseType_t watermark = uxTaskGetStackHighWaterMark(NULL);
            LOG_I("LVGL", "flush=%lu, stack_wm=%lu", (unsigned long)lv_port_disp_get_flush_count(),
                  (unsigned long)watermark);
            last_report_tick = hal_get_tick_ms();
        }
        /* 每 500ms 更新右上角的 label资源显示 */
        if ((hal_get_tick_ms() - last_res_tick) >= 500U) {
            resource_mon_update_label(res_label);
            last_res_tick = hal_get_tick_ms();
        }
        osDelay(20);
    }
    /* USER CODE END StartTask_LCD */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */



/* USER CODE END Application */
