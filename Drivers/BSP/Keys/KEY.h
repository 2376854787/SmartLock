#ifndef __KEY_H__
#define __KEY_H__
#include  "main.h"
#include "HFSM.h"
#include "log.h"
#include "config_cus.h"
/* 默认 TAG，可以按需改 */
#ifndef KEY_LOG_TAG
#define KEY_LOG_TAG  "KEY"
#endif

/**************************************************************************/
/*                            日志配置区域                                 */
/**************************************************************************/

/* 局部开关：取消注释则开启本模块日志，注释掉则本模块完全静默 */
#define  LOG_ENABLE

/*
 * 逻辑说明：
 * 1. 优先级最高：如果 局部开关(LOG_ENABLE) 和 总开关(ENABLE_LOG_SYSTEM) 都开启 -> 使用日志系统 (log.h)
 * 2. 优先级中等：如果 只有局部开关，但没有总开关 -> 回退使用 printf (方便调试)
 * 3. 优先级最低：如果 局部开关没开 -> 所有日志宏定义为空 (不占空间)
 */

#if defined(ENABLE_LOG_SYSTEM) && defined(LOG_ENABLE)

/* --- 情况一：双开关同时开启 -> 使用工程日志系统  可能在中断中执行代码时不要使用工程日志--- */
#include "log.h"

#define KEY_LOGE(fmt, ...)  LOG_E(KEY_LOG_TAG, fmt, ##__VA_ARGS__)
#define KEY_LOGW(fmt, ...)  LOG_W(KEY_LOG_TAG, fmt, ##__VA_ARGS__)
#define KEY_LOGI(fmt, ...)  LOG_I(KEY_LOG_TAG, fmt, ##__VA_ARGS__)
#define KEY_LOGD(fmt, ...)  LOG_D(KEY_LOG_TAG, fmt, ##__VA_ARGS__)

#elif defined(LOG_ENABLE)

/* --- 情况二：只有局部开关，无总日志系统 -> 回退到 printf --- */
#include <stdio.h>

#define KEY_LOGE(fmt, ...)  printf("[E][%s] " fmt "\r\n", KEY_LOG_TAG, ##__VA_ARGS__)
#define KEY_LOGW(fmt, ...)  printf("[W][%s] " fmt "\r\n", KEY_LOG_TAG, ##__VA_ARGS__)
#define KEY_LOGI(fmt, ...)  printf("[I][%s] " fmt "\r\n", KEY_LOG_TAG, ##__VA_ARGS__)
#define KEY_LOGD(fmt, ...)  printf("[D][%s] " fmt "\r\n", KEY_LOG_TAG, ##__VA_ARGS__)

#else

/* --- 情况三：局部开关关闭 -> 全部定义为空 (无代码生成) --- */
#define KEY_LOGE(fmt, ...)
#define KEY_LOGW(fmt, ...)
#define KEY_LOGI(fmt, ...)
#define KEY_LOGD(fmt, ...)

#endif
/**************************************************************************/
struct KEY_TypedefHandle; /* 向前声明按键结构体 */
typedef struct KEY_TypedefHandle KEY_TypedefHandle;

/* 按键回调事件 */
typedef enum {
    KEY_ACTION_SINGLE_CLICK,
    KEY_ACTION_DOUBLE_CLICK,
    KEY_ACTION_TRIPLE_CLICK,
    KEY_ACTION_LONG_PRESS,
    KEY_ACTION_LONG_PRESS_REPEAT, // 长按保持过程中的重复触发
} KEY_ActionType;

/* 按键事件回调 */
typedef void (*KEY_Callback)(KEY_TypedefHandle *key, KEY_ActionType action);

/*按键外部触发事件*/
typedef enum {
    KEY_Event_Pressed,
    KEY_Event_up,
    KEY_Event_OverTime,
} KEY_Event;


/*端口结构体*/
typedef struct {
    GPIO_TypeDef *GPIOx;
    uint16_t GPIO_Pin;
} KeyInfo;


/* 按键配置 */
typedef struct {
    const char *name;
    const KeyInfo *keyinfo;
    bool active_level;

    uint16_t debounce_ms; // 消抖时间
    uint16_t long_press_ms; // 长按时间
    uint16_t multi_click_ms; // 连击超时时间
    KEY_Callback callback;
    void *user_data;
} KEY_Config_t;

/*按键句柄*/
typedef struct KEY_TypedefHandle {
    const char *Key_name; /*按键名称*/
    const KeyInfo *keyinfo; /*按键信息*/
    bool active_level; /*有效电平*/
    uint16_t debounce_ms; /* 消抖时间 */
    uint16_t long_press_ms; /* 长按时间 */
    uint16_t multi_click_ms; /* 多击间隔时间设置 */
    uint8_t click_count; /*点击次数*/
    volatile bool last_key_state; /*上次按键状态*/
    volatile uint32_t timer_counter; /*时间阈值*/
    StateMachine fsm; /*状态机*/
    KEY_Callback callback; /*回调函数指针*/
    void *user_data;
} KEY_TypedefHandle;

void KEY_Init(KEY_TypedefHandle *key, const KEY_Config_t *cfg);

void KEY_Tasks(void);

void KEY_Tick_Handler(void);

// 外部声明

/*状态机定时器变量*/

#endif
