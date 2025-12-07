#include "KEY.h"
#include <stdio.h> // 用于 printf 调试

#include "MyPrintf.h"
/********************************************************************************************************************************************
 ********************************************************************************************************************************************
 ********************************************************************************************************************************************


    ************************************************ KEY状态机相关变量定义 ************************************************
    使用本模块必须实现超时事件的获取，即通过定时器中断获取标志位 overtime_flag 产生超时事件


 */


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


/* 空闲状态的事件和函数绑定 */
const EventAction_t IDLE_Event_Action[] = {
    {KEY_Event_Pressed, IDLE_Eventhandle},
    {-1, NULL}
};
const EventAction_t ELIMINATE_DITHERING_Event_Action[] = {
    {KEY_Event_OverTime, ELIMINATE_DITHERING_EventHandle},
    {-1, NULL}
};
const EventAction_t WAITING_RELEASE_Event_Action[] = {
    {KEY_Event_up, WAITING_RELEASE_EventHandle},
    {KEY_Event_OverTime, WAITING_RELEASE_EventHandle},
    {-1, NULL}
};
const EventAction_t WAITING_NEXTCLICK_Event_Action[] = {
    {KEY_Event_Pressed, WAITING_NEXTCLICK_EventHandle},
    {KEY_Event_OverTime, WAITING_NEXTCLICK_EventHandle},
    {-1, NULL}
};

/*创建状态实例*/
State IDLE = {"空闲状态", IDLE_entry, NULL, IDLE_Event_Action, NULL};
State ELIMINATE_DITHERING = {
    "消抖状态", ELIMINATE_DITHERING_entry, NULL, ELIMINATE_DITHERING_Event_Action, NULL
};
State WAITING_RELEASE = {
    "等待释放状态", WAITING_RELEASE_entry, NULL, WAITING_RELEASE_Event_Action, NULL
};
State WAITING_NEXTCLICK = {
    "等待下一次点击状态", WAITING_NEXTCLICK_entry, NULL, WAITING_NEXTCLICK_Event_Action, NULL
};

/* 最终结算的按键状态 */
State SINGLE_CLICK = {"单击状态", SING_CLICK_entry, NULL, NULL, NULL};
State DOUBLE_CLICK = {"双击状态", DOUBLE_CLICK_entry, NULL, NULL, NULL};
State TRIPLE_CLICK = {"三击状态", TRIPLE_CLICK_entry, NULL, NULL, NULL};
State LONGPRESS = {"长按状态", LONG_PRESS_entry, NULL, NULL, NULL};

/*按键数组 存储用于初始化的按键和 状态信息*/
static KEY_TypedefHandle *registered_keys[5] = {NULL};
uint8_t registered_key_count = 0;
uint8_t MAX_REGISTERED_KEYS = sizeof(registered_keys) / sizeof(registered_keys[0]);


/************************************************ GPIO内部操作函数 ************************************************/
/**
 * @brief 向指定引脚写入电平
 */
static void KEY_Pin_Write(KeyInfo *dht_pin, bool state) {
#ifdef USE_HAL_DRIVER
    HAL_GPIO_WritePin(dht_pin->GPIOx, dht_pin->GPIO_Pin, (state ? GPIO_PIN_SET : GPIO_PIN_RESET));
#else // 使用标准库 SPL
    if (state) {
        GPIO_SetBits(dht_pin->GPIOx, dht_pin->GPIO_Pin);
    } else {
        GPIO_ResetBits(dht_pin->GPIOx, dht_pin->GPIO_Pin);
    }
#endif
}

/**
 * @brief 读取指定引脚的电平
 */
static bool KEY_Pin_Read(KeyInfo *dht_pin) {
#ifdef USE_HAL_DRIVER
    return (HAL_GPIO_ReadPin(dht_pin->GPIOx, dht_pin->GPIO_Pin) == GPIO_PIN_SET);
#else // 使用标准库 SPL
    return (GPIO_ReadInputDataBit(dht_pin->port, dht_pin->pin) == Bit_SET);
#endif
}

/************************************************ KEY状态函数定义 ************************************************/
/**
 * @brief 进入空闲状态的执行函数
 * @param fsm 状态机指针
 */
void IDLE_entry(StateMachine *fsm) {
    KEY_TypedefHandle *key = (KEY_TypedefHandle *) fsm->customizeHandle;
    printf("按键：%s->进入空闲状态\n", key->Key_name);

    if (key) {
        key->click_count = 0; // 清零点击计数
    }
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
    printf("按键：%s->进入消抖状态\n", key->Key_name);
    Key_Timer_Start(key, 20); // 启动20ms消抖定时
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
    printf("按键：%s->进入等待按键释放状态\n", key->Key_name);
    Key_Timer_Start(key, 800); // 启动1000ms长按定时
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
    printf("按键：%s->进入等待下一次点击状态\n", key->Key_name);
    // 开始计时
    Key_Timer_Start(key, 300); // 启动300ms连击超时定时
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
                    printf("出现异常\n");
                    HFSM_Transition(fsm, (State *) &IDLE);
            }
            break;
        default: printf("出现未定义行为\n");;
    }
    return false;
}

/****************************** 最终状态的处理函数 *******************************/
static void SING_CLICK_entry(StateMachine *fsm) {
    KEY_TypedefHandle *key = (KEY_TypedefHandle *) (fsm->customizeHandle);
    printf("按键：%s->进入单击状态\n", ((KEY_TypedefHandle *) (fsm->customizeHandle))->Key_name);
    // 执行逻辑
    if (key->callback) {
        key->callback(key, KEY_ACTION_SINGLE_CLICK);
    }
    // 进入休闲状态
    HFSM_Transition(fsm, (State *) &IDLE);
}

static void DOUBLE_CLICK_entry(StateMachine *fsm) {
    KEY_TypedefHandle *key = (KEY_TypedefHandle *) (fsm->customizeHandle);
    printf("按键：%s->进入双击状态\n", ((KEY_TypedefHandle *) (fsm->customizeHandle))->Key_name);
    // 执行逻辑
    if (key->callback) {
        key->callback(key, KEY_ACTION_DOUBLE_CLICK);
    }
    // 进入休闲状态
    HFSM_Transition(fsm, (State *) &IDLE);
}

static void TRIPLE_CLICK_entry(StateMachine *fsm) {
    KEY_TypedefHandle *key = (KEY_TypedefHandle *) (fsm->customizeHandle);
    printf("按键：%s->进入三击状态\n", ((KEY_TypedefHandle *) (fsm->customizeHandle))->Key_name);
    // 执行逻辑
    if (key->callback) {
        key->callback(key, KEY_ACTION_TRIPLE_CLICK);
    }
    // 进入休闲状态
    HFSM_Transition(fsm, (State *) &IDLE);
}

static void LONG_PRESS_entry(StateMachine *fsm) {
    KEY_TypedefHandle *key = (KEY_TypedefHandle *) (fsm->customizeHandle);
    printf("按键：%s->进入长按状态\n", ((KEY_TypedefHandle *) (fsm->customizeHandle))->Key_name);
    // 执行逻辑
    if (key->callback) {
        key->callback(key, KEY_ACTION_LONG_PRESS);
    }
    // 进入休闲状态
    HFSM_Transition(fsm, (State *) &IDLE);
}

/**************************提供给外部的函数*************************/

/**
 * @brief 启动按键状态机定时器
 * @param key           指向调用此定时器的按键句柄
 * @param timeout_ms    超时时间 (毫秒)
 */
void Key_Timer_Start(KEY_TypedefHandle *key, uint32_t timeout_ms) {
    __disable_irq();
    key->timer_counter = timeout_ms;
    __enable_irq();
}

/**
 * @brief 停止按键状态机定时器
 * @param key           指向要停止定时器的按键句柄
 */
void Key_Timer_Stop(KEY_TypedefHandle *key) {
    key->timer_counter = 0;
}


/**
 * @brief 状态机和 按键句柄的初始化
 * @param key 指向要初始化的按键句柄
 */
void KEY_Init(KEY_TypedefHandle *key) {
    // 1、让状态机内部的自定义指针指回其容器（key句柄），以便在状态函数中访问
    key->fsm.customizeHandle = key;
    key->fsm.fsm_name = key->Key_name;
    // 2、 初始化内嵌的状态机
    HFSM_Init(&key->fsm, (State *) &IDLE);


    // 注册按键到全局管理数组
    if (registered_key_count < MAX_REGISTERED_KEYS) {
        registered_keys[registered_key_count++] = key;
    } else {
        printf("KEY_Init: 按键注册数组已满，无法添加新按键\n");
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

    printf("KEY_Init: 按键 %s 初始化完成，初始状态: %d\r\n", key->Key_name, key->last_key_state);
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

        const uint8_t current_key_state = KEY_Pin_Read(key->keyinfo);
        // 检测按键状态变化
        if (current_key_state != key->last_key_state) {
            printf("!!! 按键 %s 电平变化: 从 %d 变为 %d !!!\r\n", key->Key_name, key->last_key_state, current_key_state);

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
