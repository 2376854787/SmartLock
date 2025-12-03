#ifndef __KEY_H__
#define __KEY_H__
#include  "main.h"
#include "HFSM.h"

/*按键外部触发事件*/
typedef enum {
    KEY_Event_Idle,
    KEY_Event_Pressed,
    KEY_Event_up,
    KEY_Event_OverTime,
} KEY_Event;

/*端口结构体*/
typedef struct {
    GPIO_TypeDef *GPIOx;
    uint16_t GPIO_Pin;
} KeyInfo;

/*按键句柄*/
typedef struct {
    const char *Key_name;            /*按键名称*/
    KeyInfo *keyinfo;                /*按键信息*/
    bool active_level;               /*有效电平*/
    uint8_t click_count;             /*点击次数*/
    bool last_key_state;             /*上次按键状态*/
    bool overtime_flag;              /*超时标志*/
    volatile uint32_t timer_counter; /*时间阈值*/
    StateMachine fsm;               /*状态机*/
    EventFunc callback;              /*回调函数指针*/
} KEY_TypedefHandle;
void KEY_Init(KEY_TypedefHandle *key);
void Key_Timer_Start(KEY_TypedefHandle *key, uint32_t timeout_ms);
void Key_Timer_Stop(KEY_TypedefHandle *key);
void KEY_Tasks(void);
void KEY_Tick_Handler(void);
// 外部声明
extern State IDLE;
extern State ELIMINATE_DITHERING;
extern State WAITING_RELEASE;
extern State WAITING_NEXTCLICK;
extern State SING_CLICK;
extern State DOUBLE_CLICK;
extern State TRIPLE_CLICK;
extern State LONGPRESS;
extern uint8_t overtime_flag;
/*状态机定时器变量*/

#endif