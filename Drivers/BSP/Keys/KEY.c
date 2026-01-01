#include "KEY.h"
/* 全局代码启用配置宏 */
#ifdef  ENABLE_KEYS
#include <stdint.h>

#include "cmsis_gcc.h"
#include "HFSM.h"
#include "stm32f4xx_hal_gpio.h"

#include "KEY.h"
#include <stdio.h> // 用于 printf 调试

/********************************************************************************************************************************************
 ********************************************************************************************************************************************
 ********************************************************************************************************************************************


    ************************************************ KEY状态机相关变量定义 ************************************************
    使用本模块必须实现超时事件的获取
 */


/* 内部宏定义 */
#define KEY_DEBOUNCE_MS         20u
#define KEY_LONG_PRESS_MS       800u
#define KEY_MULTI_CLICK_MS      300u


/* 函数声明 */
static void Key_Timer_Start(KEY_TypedefHandle *key, uint32_t timeout_ms);

static void Key_Timer_Stop(KEY_TypedefHandle *key);

void IDLE_entry(StateMachine *fsm);

static bool IDLE_Eventhandle(StateMachine *fsm, const Event *event);

static void ELIMINATE_DITHERING_entry(StateMachine *fsm);

static bool ELIMINATE_DITHERING_EventHandle(StateMachine *fsm, const Event *event);

static void WAITING_RELEASE_entry(StateMachine *fsm);

static bool WAITING_RELEASE_EventHandle(StateMachine *fsm, const Event *event);

static void WAITING_NEXTCLICK_entry(StateMachine *fsm);

static bool WAITING_NEXTCLICK_EventHandle(StateMachine *fsm, const Event *event);

static void SING_CLICK_entry(StateMachine *fsm);

static void DOUBLE_CLICK_entry(StateMachine *fsm);

static void TRIPLE_CLICK_entry(StateMachine *fsm);

static void LONG_PRESS_entry(StateMachine *fsm);

static void LONG_PRESS_HOLD_entry(StateMachine *fsm);

static bool LONGPRESS_HOLD_EventHandle(StateMachine *fsm, const Event *event);

/* 事件和回调函数映射表设置 */
/* 空闲状态的事件和函数绑定 */
static const EventAction_t IDLE_Event_Action[] = {
    {.event_id = KEY_Event_Pressed, .handler = IDLE_Eventhandle},
    {.event_id = -1, .handler = NULL}
};
static const EventAction_t ELIMINATE_DITHERING_Event_Action[] = {
    {.event_id = KEY_Event_OverTime, .handler = ELIMINATE_DITHERING_EventHandle},
    {.event_id = -1, .handler = NULL}
};
static const EventAction_t WAITING_RELEASE_Event_Action[] = {
    {.event_id = KEY_Event_up, .handler = WAITING_RELEASE_EventHandle},
    {.event_id = KEY_Event_OverTime, .handler = WAITING_RELEASE_EventHandle},
    {.event_id = -1, .handler = NULL}
};
static const EventAction_t WAITING_NEXTCLICK_Event_Action[] = {
    {.event_id = KEY_Event_Pressed, .handler = WAITING_NEXTCLICK_EventHandle},
    {.event_id = KEY_Event_OverTime, .handler = WAITING_NEXTCLICK_EventHandle},
    {.event_id = -1, .handler = NULL}
};
static const EventAction_t LONGPRESS_HOLD_Event_Action[] = {
    {.event_id = KEY_Event_OverTime, .handler = LONGPRESS_HOLD_EventHandle},
    {.event_id = KEY_Event_up, .handler = LONGPRESS_HOLD_EventHandle},
    {.event_id = -1, .handler = NULL}
};

/*创建状态实例*/
static const State IDLE = {
    .state_name = "空闲状态", .on_enter = IDLE_entry, .on_exit = NULL, .event_actions = IDLE_Event_Action, .parent = NULL
};
static const State ELIMINATE_DITHERING = {
    .state_name = "消抖状态", .on_enter = ELIMINATE_DITHERING_entry, .on_exit = NULL,
    .event_actions = ELIMINATE_DITHERING_Event_Action,
    .parent = NULL
};
static const State WAITING_RELEASE = {
    .state_name = "等待释放状态", .on_enter = WAITING_RELEASE_entry, .on_exit = NULL,
    .event_actions = WAITING_RELEASE_Event_Action, .parent = NULL
};
static const State WAITING_NEXTCLICK = {
    .state_name = "等待下一次点击状态", .on_enter = WAITING_NEXTCLICK_entry, .on_exit = NULL,
    .event_actions = WAITING_NEXTCLICK_Event_Action,
    .parent = NULL
};

/* 最终结算的按键状态 */
static const State SINGLE_CLICK = {
    .state_name = "单击状态", .on_enter = SING_CLICK_entry, .on_exit = NULL, .event_actions = NULL, .parent = NULL
};
static const State DOUBLE_CLICK = {
    .state_name = "双击状态", .on_enter = DOUBLE_CLICK_entry, .on_exit = NULL, .event_actions = NULL, .parent = NULL
};
static const State TRIPLE_CLICK = {
    .state_name = "三击状态", .on_enter = TRIPLE_CLICK_entry, .on_exit = NULL, .event_actions = NULL, .parent = NULL
};
static const State LONGPRESS = {
    .state_name = "长按状态", .on_enter = LONG_PRESS_entry, .on_exit = NULL, .event_actions = NULL, .parent = NULL
};
static const State LONGPRESS_HOLD = {
    .state_name = "长按保持", .on_enter = LONG_PRESS_HOLD_entry, .on_exit = NULL,
    .event_actions = LONGPRESS_HOLD_Event_Action, .parent = NULL
};
/*按键数组 存储用于初始化的按键和 状态信息*/
static KEY_TypedefHandle *registered_keys[5] = {NULL};
static uint8_t registered_key_count = 0;
static const uint8_t MAX_REGISTERED_KEYS = sizeof(registered_keys) / sizeof(registered_keys[0]);


/************************************************ GPIO内部操作函数 ************************************************/
/**
 * @brief 向指定引脚写入电平
 */
static void KEY_Pin_Write(const KeyInfo *pin, bool state) {
#ifdef USE_HAL_DRIVER
    HAL_GPIO_WritePin(pin->GPIOx, pin->GPIO_Pin,
                      state ? GPIO_PIN_SET : GPIO_PIN_RESET);
#else // 使用标准库 SPL
    if (state) {
        GPIO_SetBits(pin->GPIOx, pin->GPIO_Pin);
    } else {
        GPIO_ResetBits(pin->GPIOx, pin->GPIO_Pin);
    }
#endif
}


/**
 * @brief 读取指定引脚的电平
 */
static bool KEY_Pin_Read(const KeyInfo *pin) {
#ifdef USE_HAL_DRIVER
    return (HAL_GPIO_ReadPin(pin->GPIOx, pin->GPIO_Pin) == GPIO_PIN_SET);
#else // 使用标准库 SPL
    return (GPIO_ReadInputDataBit(pin->GPIOx, pin->GPIO_Pin) == Bit_SET);
#endif
}


/************************************************ KEY状态函数定义 ************************************************/
/**
 * @brief 进入空闲状态的执行函数
 * @param fsm 状态机指针
 */
void IDLE_entry(StateMachine *fsm) {
    KEY_TypedefHandle *key = (KEY_TypedefHandle *) fsm->customizeHandle;
    if (!key) {
        KEY_LOGE("KEY指针为NULL！");
        return;
    }
    KEY_LOGI("按键：%s->进入空闲状态\n", key->Key_name);


    key->click_count = 0; // 清零点击计数
}

/**
 * @brief 在空闲状态的事件执行函数
 * @param fsm 状态机指针
 * @param event 触发事件
 * @return 事件是否被处理
 */
static bool IDLE_Eventhandle(StateMachine *fsm, const Event *event) {
    if (event->event_id == KEY_Event_Pressed) {
        HFSM_Transition(fsm, (State *) &ELIMINATE_DITHERING);
        return true;
    }
    return false;
}

/**
 * @brief 进入消抖状态的执行函数
 * @param fsm 状态机指针
 */
static void ELIMINATE_DITHERING_entry(StateMachine *fsm) {
    KEY_TypedefHandle *key = (KEY_TypedefHandle *) fsm->customizeHandle;
    if (!key) {
        //打印
        KEY_LOGE("KEY指针为NULL！");
        return;
    }
    KEY_LOGI("按键：%s->进入消抖状态\n", key->Key_name);
    Key_Timer_Start(key, key->debounce_ms); // 启动消抖定时（默认20ms）
}

/**
 * @brief 在消抖状态的事件执行函数
 * @param fsm 状态机指针
 * @param event 触发事件
 * @return 事件是否被处理
 */
static bool ELIMINATE_DITHERING_EventHandle(StateMachine *fsm, const Event *event) {
    KEY_TypedefHandle *key = (KEY_TypedefHandle *) fsm->customizeHandle;
    switch (event->event_id) {
        case KEY_Event_OverTime:
            // 读取电平判断
            if (KEY_Pin_Read(key->keyinfo) == key->active_level) {
                HFSM_Transition(fsm, (State *) &WAITING_RELEASE); // 消抖后还是活动电平判定按下按键
            } else {
                HFSM_Transition(fsm, (State *) &IDLE); // 毛刺回到空闲
            }

            return true;

        default:
            break;
    }
    return false;
}

/**
 * @brief 进入等待按键释放状态的执行函数
 * @param fsm 状态机指针
 */
static void WAITING_RELEASE_entry(StateMachine *fsm) {
    KEY_TypedefHandle *key = (KEY_TypedefHandle *) fsm->customizeHandle;
    if (!key) {
        //打印
        KEY_LOGE("KEY指针为NULL！");
        return;
    }
    KEY_LOGI("按键：%s->进入等待按键释放状态\n", key->Key_name);
    Key_Timer_Start(key, key->long_press_ms); // 启动800（默认）ms长按定时
}

/**
 * @brief 在等待按键释放状态的事件执行函数
 * @param fsm 状态机指针
 * @param event 触发事件
 * @return 事件是否被处理
 */
static bool WAITING_RELEASE_EventHandle(StateMachine *fsm, const Event *event) {
    KEY_TypedefHandle *key = (KEY_TypedefHandle *) fsm->customizeHandle;
    switch (event->event_id) {
        case KEY_Event_up: // 进入计次+1 最后统一处理
            key->click_count++;
            Key_Timer_Stop(key); // 提前抬起，停止长按定时器
            HFSM_Transition(fsm, (State *) &WAITING_NEXTCLICK);
            return true;
        case KEY_Event_OverTime: // 超时后判定为长按
            HFSM_Transition(fsm, (State *) &LONGPRESS);
            return true;

        default:
            break;
    }
    return false;
}

/**
 * @brief 等待下一次点击状态的执行函数
 * @param fsm 状态机指针
 */
static void WAITING_NEXTCLICK_entry(StateMachine *fsm) {
    KEY_TypedefHandle *key = (KEY_TypedefHandle *) fsm->customizeHandle;
    if (!key) {
        //打印
        KEY_LOGE("KEY指针为NULL！");
        return;
    }
    KEY_LOGI("按键：%s->进入等待下一次点击状态\n", key->Key_name);
    // 开始计时
    Key_Timer_Start(key, key->multi_click_ms); // 启动300ms（默认）连击超时定时
}

/**
 * @brief 在等待下一次点击状态的事件执行函数
 * @param fsm 状态机指针
 * @param event 触发事件
 * @return 事件是否被处理
 */
static bool WAITING_NEXTCLICK_EventHandle(StateMachine *fsm, const Event *event) {
    KEY_TypedefHandle *key = (KEY_TypedefHandle *) fsm->customizeHandle;
    switch (event->event_id) {
        case KEY_Event_Pressed: // 再次进入消抖
            Key_Timer_Stop(key); // 新的点击来了，停止连击超时
            HFSM_Transition(fsm, (State *) &ELIMINATE_DITHERING);
            return true;
        case KEY_Event_OverTime: // 进行最终的单击次数判断
            switch (((KEY_TypedefHandle *) (fsm->customizeHandle))->click_count) {
                case 1:
                    HFSM_Transition(fsm, (State *) &SINGLE_CLICK);
                    return true;

                case 2:
                    HFSM_Transition(fsm, (State *) &DOUBLE_CLICK);
                    return true;
                case 3:
                    HFSM_Transition(fsm, (State *) &TRIPLE_CLICK);
                    return true;
                default:
                    KEY_LOGE("出现异常/未定义行为\n");
                    HFSM_Transition(fsm, (State *) &IDLE);
            }
            break;
        default: KEY_LOGE("出现未定义事件\n");;
    }
    return false;
}


/**
 * @brief 判断长按保持状态下对事件的处理
 * @param fsm 状态机句柄
 * @param event 发生的事件
 * @return 返回是否成功
 */
static bool LONGPRESS_HOLD_EventHandle(StateMachine *fsm, const Event *event) {
    KEY_TypedefHandle *key = (KEY_TypedefHandle *) fsm->customizeHandle;
    if (!key) return false;

    switch (event->event_id) {
        case KEY_Event_OverTime:
            /* 每次超时，触发一次 repeat */
            if (key->callback) {
                key->callback(key, KEY_ACTION_LONG_PRESS_REPEAT);
            }
            Key_Timer_Start(key, key->multi_click_ms);
            return true;

        case KEY_Event_up:
            /* 松手，退出长按保持，回 IDLE */
            Key_Timer_Stop(key);
            HFSM_Transition(fsm, &IDLE);
            return true;

        default:
            return false;
    }
}

/****************************** 最终状态的处理函数 *******************************/
static void SING_CLICK_entry(StateMachine *fsm) {
    KEY_TypedefHandle *key = (KEY_TypedefHandle *) (fsm->customizeHandle);
    KEY_LOGI("按键：%s->进入单击状态\n", ((KEY_TypedefHandle *) (fsm->customizeHandle))->Key_name);
    // 执行逻辑
    if (key->callback) {
        key->callback(key, KEY_ACTION_SINGLE_CLICK);
    }
    // 进入休闲状态
    HFSM_Transition(fsm, (State *) &IDLE);
}

static void DOUBLE_CLICK_entry(StateMachine *fsm) {
    KEY_TypedefHandle *key = (KEY_TypedefHandle *) (fsm->customizeHandle);
    KEY_LOGI("按键：%s->进入双击状态\n", ((KEY_TypedefHandle *) (fsm->customizeHandle))->Key_name);
    // 执行逻辑
    if (key->callback) {
        key->callback(key, KEY_ACTION_DOUBLE_CLICK);
    }
    // 进入休闲状态
    HFSM_Transition(fsm, (State *) &IDLE);
}

static void TRIPLE_CLICK_entry(StateMachine *fsm) {
    KEY_TypedefHandle *key = (KEY_TypedefHandle *) (fsm->customizeHandle);
    KEY_LOGI("按键：%s->进入三击状态\n", ((KEY_TypedefHandle *) (fsm->customizeHandle))->Key_name);
    // 执行逻辑
    if (key->callback) {
        key->callback(key, KEY_ACTION_TRIPLE_CLICK);
    }
    // 进入休闲状态
    HFSM_Transition(fsm, (State *) &IDLE);
}

static void LONG_PRESS_entry(StateMachine *fsm) {
    KEY_TypedefHandle *key = (KEY_TypedefHandle *) fsm->customizeHandle;
    if (!key) return;

    KEY_LOGI("按键：%s -> 长按触发", key->Key_name);
    if (key->callback) {
        key->callback(key, KEY_ACTION_LONG_PRESS);
    }
    // 进入“长按保持”状态
    HFSM_Transition(fsm, (State *) &LONGPRESS_HOLD);
}

static void LONG_PRESS_HOLD_entry(StateMachine *fsm) {
    KEY_TypedefHandle *key = (KEY_TypedefHandle *) (fsm->customizeHandle);
    KEY_LOGI("按键：%s->进入长按保持状态\n", ((KEY_TypedefHandle *) (fsm->customizeHandle))->Key_name);
    if (key->callback) {
        key->callback(key, KEY_ACTION_LONG_PRESS_REPEAT);
    }
    // 进入休闲状态
    Key_Timer_Start(key, key->multi_click_ms); // 例如 100ms
}

/**************************提供给外部的函数*************************/

/**
 * @brief 启动按键状态机定时器
 * @param key           指向调用此定时器的按键句柄
 * @param timeout_ms    超时时间 (毫秒)
 */
static void Key_Timer_Start(KEY_TypedefHandle *key, uint32_t timeout_ms) {
    __disable_irq();
    key->timer_counter = timeout_ms;
    __enable_irq();
}

/**
 * @brief 停止按键状态机定时器
 * @param key           指向要停止定时器的按键句柄
 */
static void Key_Timer_Stop(KEY_TypedefHandle *key) {
    key->timer_counter = 0;
}

/**
 * @brief 通过一个按键配置结构体，简化按键初始化
 * @param key 按键句柄
 * @param cfg 按键配置结构体
 */
void KEY_Init(KEY_TypedefHandle *key, const KEY_Config_t *cfg) {
    key->Key_name = cfg->name;
    key->keyinfo = cfg->keyinfo;
    key->active_level = cfg->active_level;
    key->debounce_ms = cfg->debounce_ms;
    key->long_press_ms = cfg->long_press_ms;
    key->multi_click_ms = cfg->multi_click_ms;
    key->callback = cfg->callback;
    key->user_data = cfg->user_data;

    // 1、让状态机内部的自定义指针指回其容器（key句柄），以便在状态函数中访问
    key->fsm.customizeHandle = key;
    key->fsm.fsm_name = key->Key_name;
    // 2、 初始化内嵌的状态机
    HFSM_Init(&key->fsm, (State *) &IDLE);


    // 注册按键到全局管理数组
    if (registered_key_count < MAX_REGISTERED_KEYS) {
        registered_keys[registered_key_count++] = key;
    } else {
        KEY_LOGW("KEY_Init: 按键注册数组已满，无法添加新按键\n");
    }

#ifdef USE_HAL_DRIVER
    /* 非上拉输入 低电平有效 注释下面这句代码 */
    key->last_key_state = (KEY_Pin_Read(key->keyinfo) != key->active_level);
#else
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE); // 使能GPIOB时钟
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = key->keyinfo->GPIO_Pin;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU; // 上拉输入
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(key->keyinfo->GPIOx, &GPIO_InitStructure);

    // 初始化按键的上一次状态，防止程序启动时误触发
    key->last_key_state = (KEY_Pin_Read(key->keyinfo) != key->active_level);
#endif

    KEY_LOGI("KEY_Init: 按键 %s 初始化完成，初始状态: %d\r\n", key->Key_name, key->last_key_state);
}

/**
 * @brief 按键定时器滴答处理函数
 * @note  此函数应在1ms定时器中断中（如SysTick_Handler）被调用
 */
void KEY_Tick_Handler(void) {
    // 遍历所有已注册的按键
    for (uint8_t i = 0; i < registered_key_count; i++) {
        KEY_TypedefHandle *key = registered_keys[i];
        if (key->timer_counter > 0) {
            key->timer_counter--;
            if (key->timer_counter == 0) {
                // 定时器时间到，为对应的状态机生成一个超时事件
                Event overtime_event = {KEY_Event_OverTime, NULL};
                HFSM_HandleEvent(&key->fsm, &overtime_event);
            }
        }
    }
}

/**
 * @brief 扫描所有按键状态变化并触发相应事件
 * @note  此函数应在主循环中被周期性调用
 */
void KEY_Tasks(void) {
    // 遍历所有已注册的按键
    for (uint8_t i = 0; i < registered_key_count; i++) {
        KEY_TypedefHandle *key = registered_keys[i];
        StateMachine *fsm = &key->fsm; // 获取当前按键对应的状态机指针

        const bool current_key_state = KEY_Pin_Read(key->keyinfo);
        // 检测按键状态变化
        if (current_key_state != key->last_key_state) {
            KEY_LOGD("!!! 按键 %s 电平变化: 从 %d 变为 %d !!!\r\n", key->Key_name, key->last_key_state, current_key_state);

            if (current_key_state == key->active_level) {
                Event press_event = {KEY_Event_Pressed, NULL};
                HFSM_HandleEvent(fsm, &press_event);
            } else {
                Event up_event = {KEY_Event_up, NULL};
                HFSM_HandleEvent(fsm, &up_event);
            }
            key->last_key_state = current_key_state;
        }
    }
}


#endif
