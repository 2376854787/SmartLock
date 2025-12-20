#include "APP_config.h"
/* 全局配置开启宏 */
#ifdef ENABLE_AT_SYSTEM
#include "AT.h"
#include <string.h>

#ifndef AT_UART_MAX
#define AT_UART_MAX  4
#endif

typedef struct {
    UART_HandleTypeDef *huart;
    AT_Manager_t *mgr;
} AT_UartBind_t;

static AT_UartBind_t s_binds[AT_UART_MAX];

/**
 * @brief 映射串口和AT设备句柄 打通 串口 -》》 AT设备句柄
 * @param mgr AT设备句柄
 * @param huart 串口句柄
 */
void AT_BindUart(AT_Manager_t *mgr, UART_HandleTypeDef *huart) {
    if (!mgr || !huart) return;

    /* 更新/插入 */
    for (uint32_t i = 0; i < AT_UART_MAX; i++) {
        if (s_binds[i].huart == huart || s_binds[i].huart == NULL) {
            s_binds[i].huart = huart;
            s_binds[i].mgr = mgr;
            mgr->uart = huart;
            return;
        }
    }
    /* 满了就报错 */
    LOG_E("AT", "AT_BindUart bind table full");
}

/**
 *
 * @param huart 串口句柄
 * @return 返回查询到的AT设备句柄
 */
AT_Manager_t *AT_FindMgrByUart(const UART_HandleTypeDef *huart) {
    if (!huart) return NULL;
    for (uint32_t i = 0; i < AT_UART_MAX; i++) {
        if (s_binds[i].huart && s_binds[i].huart->Instance == huart->Instance) {
            return s_binds[i].mgr;
        }
    }
    return NULL;
}



#endif
