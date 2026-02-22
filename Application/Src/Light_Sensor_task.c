//
// Created by yan on 2025/11/15.
//

#include "Light_Sensor_task.h"

#include <stdio.h>

#include "Beep.h"
#include "LightSensor.h"
#include "log.h"
#include "osal.h"
#include "watchdog_app.h"
#include "wdg_supervisor.h"

void StartLightSensorTask(void *argument) {
    LightSensor_Init();
    uint8_t id=0;
    wdg_sup_register(&id, "light sensor task", WDG_WATCH_CHALLENGE, WDG_ALGO_MATH_MIX32, 2, 3,
                    2 * 1000 + CFG_PARAM_WATCHDOG_APP_SUP_PERIOD_MS + 50);
    for (;;) {
        LightSensor_Init();
        LightSensor_Data = LightSensor_Read();
        // if (LightSensor_Data<2000) Beep_control(1);
        // else Beep_control(0);
       //char buffer[64];
        //sniprintf(buffer, sizeof(buffer), "当前光敏电阻值为 %u\r\n", (unsigned)LightSensor_Data);
       // uart send path is handled by log/hal uart abstraction.
        //LOG_D("光敏","当前光敏电阻值为 %u\r\n", (unsigned)LightSensor_Data);
        //LOG_HEX("哈哈",LOG_LEVEL_ERROR,"666@",6);
        wdg_sup_task_service(id);
        (void)OSAL_delay_ms(1000); // 1s 读一次，完全够用
    }
}
