
#include "HFSM.h"
#include <stddef.h>
#include <stdio.h> // 用于 printf 调试

#include "MyPrintf.h"
/**
 * @brief 初始化状态机
 * @param fsm 指向状态机实例的指针
 * @param initial_state 指向初始状态的指针
 */
void HFSM_Init(StateMachine *fsm, State *initial_state)
{
    if (fsm == NULL || initial_state == NULL) {
        printf("HFSM_Init: Invalid parameters\n");
        return;
    }
    fsm->current_state = NULL;
    HFSM_Transition(fsm, initial_state);
}

/**
 * @brief 状态转换函数
 * @param fsm 指向状态机实例的指针
 * @param new_state 指向新的状态的指针
 */
void HFSM_Transition(StateMachine *fsm, State *new_state)
{
    printf("HFSM_Transition: Transitioning from %s to %s\n",
           fsm->current_state ? fsm->current_state->state_name : "NULL",
           new_state->state_name);
    // 转换到新的状态、
    State *s = fsm->current_state;
    while (s) {
        if (s && s != new_state->parent && s != new_state) {
            if (s->on_exit) {
                s->on_exit(fsm);
            }
            s = s->parent;
        }
    }

    fsm->current_state = new_state;
    // 进入新状态以及进入的函数执行
    if (new_state->on_enter) {
        new_state->on_enter(fsm);
    }
}

/**
 * @brief 处理事件
 * @param fsm 指向状态机实例的指针
 * @param event 指向事件的指针
 */
void HFSM_HandleEvent(StateMachine *fsm, Event *event)
{
    printf("\n>>> Handling event: %d...\n", event->event_id);
    State *s = fsm->current_state;
    while (s) {
        if (s->handle_event) {
            bool handled = s->handle_event(fsm, event);
            if (handled) {
                printf("Event %d handled by state: %s\n", event->event_id, s->state_name);
                return;
            }
        }
        s = s->parent; // 当前状态没有响应该事件往上层状态继续传递
    }
}
