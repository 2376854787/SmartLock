//
// Created by yan on 2025/12/7.
//
#include "AT.h"

#include "log.h"
static AT_Manager_t g_AT_Manager;
static State IDLE = {
    .state_name = "AT_IDLE",
    .event_actions = NULL,
    .on_enter = NULL,
    .on_exit = NULL,
    .parent = NULL /* 没有上层状态机 */
};

void AT_Core_Init(void (*send_func)(uint8_t *, uint16_t)) {
    /* 1、接收发送命令函数指针 */
    g_AT_Manager.hw_send = send_func;

    /* 2、初始化RingBuffer缓冲区 */
    if (!CreateRingBuffer(&g_AT_Manager.rx_rb, AT_RX_RB_SIZE)) {
        LOG_E("RingBuffer", "环形缓冲区初始化失败");
    }

    /* 3、初始化 HFSM 为空闲状态*/
    HFSM_Init(&g_AT_Manager.fsm, &IDLE);

    /* 4、初始化变量 */
    g_AT_Manager.line_idx = 0;
    g_AT_Manager.curr_cmd = NULL;

    /* 5、RTOS 裸机环境分开处理 */
#if AT_RTOS_ENABLE
    /* 1. 定义互斥锁属性 (静态定义，保证属性结构体一直存在) */
    /* 属性：递归锁 + 优先级继承 */
    static const osMutexAttr_t send_mutex_attr = {
        .name = "AT_SendMutex",
        .attr_bits = osMutexRecursive | osMutexPrioInherit,
        .cb_mem = NULL,
        .cb_size = 0
    };

    /* 2. 创建互斥锁 */
    /* g_at_mgr 是你的全局管理器实例，或者通过参数传进来的指针 */
    g_AT_Manager.send_mutex = osMutexNew(&send_mutex_attr);

    if (g_AT_Manager.send_mutex == NULL) {
        /* 严重错误：互斥锁创建失败 (通常是Heap不够了) */
        LOG_E("AT", "Mutex Create Failed!");
    }
#else
    /* 裸机模式：简单复位标志位 */
    g_AT_Manager.is_locked = false;
#endif
}
