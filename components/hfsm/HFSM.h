#ifndef __HFMS_H__
#define __HFMS_H__
#include <stdbool.h>
#include "log.h"


struct StateMachine; // 提前声明
struct State;
struct Event;

//回调函数
typedef bool (*EventFunc)(struct StateMachine *, const struct Event *);

typedef void (*StateFunc)(struct StateMachine *);


/*一个通用的事件结构体*/
typedef struct Event {
    int event_id; // 事件标识符
    void *event_data; // 指向事件相关数据的指针
} Event;

// 将事件ID映射到特定的处理函数
typedef struct {
    int event_id; // 事件标识符
    EventFunc handler; //对应的事件处理函数
} EventAction_t;

/*状态结构体*/
typedef struct State {
    const char *state_name; // 状态名称（用于调试）
    StateFunc on_enter; // 进入状态时的回调函数
    StateFunc on_exit; // 退出状态时的回调函数
    const EventAction_t *event_actions; // 指向一个事件-动作映射数组
    const struct State *parent; // 指向父状态的指针（用于层次状态机）
} State;

/*状态机结构体*/
typedef struct StateMachine {
    const char *fsm_name; // 状态机名称（用于调试）
    const State *current_state; // 当前状态
    void *customizeHandle; /*自定义的数据可以指向父结构体 比如按键等硬件句柄,*/
} StateMachine;


void HFSM_Init(StateMachine *fsm, const State *initial_state);

void HFSM_Transition(StateMachine *fsm, const State *new_state);

void HFSM_HandleEvent(StateMachine *fsm, const Event *event);
#endif /*__HFMS_H__*/
