#include "HFSM.h"
#include <stddef.h>
#include <stdio.h> // 用于 printf 调试

#include "MyPrintf.h"
/**
 * @brief 初始化状态机
 * @param fsm 指向状态机实例的指针
 * @param initial_state 指向初始状态的指针
 */
void HFSM_Init(StateMachine *fsm, State *initial_state) {
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
void HFSM_Transition(StateMachine *fsm, State *new_state) {
    if (fsm == NULL || new_state == NULL) return;
    printf("HFSM_Transition: Transitioning from %s to %s\n",
           fsm->current_state ? fsm->current_state->state_name : "NULL",
           new_state->state_name);
    // 转换到新的状态、
    const State *s = fsm->current_state;
    while (s && s != new_state && s != new_state->parent) {
        if (s->on_exit) {
            s->on_exit(fsm);
        }
        s = s->parent;
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
void HFSM_HandleEvent(StateMachine *fsm, Event *event) {
    printf("\n>>> Handling event: %d...\n", event->event_id);
    const State *s = fsm->current_state;

    while (s) {
        /* 1、判断当前是否有映射表 */
        if (s->event_actions) {
            /* 2、有就遍历该表并判断当前状态是否有可以被该事件触发的事项 */
            for (int i = 0; s->event_actions[i].handler != NULL; i++) {
                const EventAction_t *act = &s->event_actions[i];
                if (act->event_id == event->event_id) {
                    bool handled = act->handler(fsm, event);
                    if (handled) {
                        printf("Event %d handled by state: %s\n",
                               event->event_id, s->state_name);
                        return;
                    }
                    // 没处理完，继续父状态
                    break;
                }
            }
        }
        s = s->parent;
    }
}
