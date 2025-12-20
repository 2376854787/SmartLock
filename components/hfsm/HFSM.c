#include "APP_config.h"
/* 全局配置开启宏 */
#ifdef ENABLE_HFSM_SYSTEM
#include "HFSM.h"
#include <stddef.h>
#include <stdio.h> // 用于 printf 调试
#include "log.h"


/* 默认 TAG，可以按需改 */
#ifndef HFSM_LOG_TAG
#define HFSM_LOG_TAG  "HFSM"
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

/* --- 情况一：双开关同时开启 -> 使用工程日志系统 --- */
#include "log.h"

#define HFSM_LOGE(fmt, ...)  LOG_E(HFSM_LOG_TAG, fmt, ##__VA_ARGS__)
#define HFSM_LOGW(fmt, ...)  LOG_W(HFSM_LOG_TAG, fmt, ##__VA_ARGS__)
#define HFSM_LOGI(fmt, ...)  LOG_I(HFSM_LOG_TAG, fmt, ##__VA_ARGS__)
#define HFSM_LOGD(fmt, ...)  LOG_D(HFSM_LOG_TAG, fmt, ##__VA_ARGS__)

#elif defined(LOG_ENABLE)

/* --- 情况二：只有局部开关，无总日志系统 -> 回退到 printf --- */
#include <stdio.h>

#define HFSM_LOGE(fmt, ...)  printf("[E][%s] " fmt "\r\n", HFSM_LOG_TAG, ##__VA_ARGS__)
#define HFSM_LOGW(fmt, ...)  printf("[W][%s] " fmt "\r\n", HFSM_LOG_TAG, ##__VA_ARGS__)
#define HFSM_LOGI(fmt, ...)  printf("[I][%s] " fmt "\r\n", HFSM_LOG_TAG, ##__VA_ARGS__)
#define HFSM_LOGD(fmt, ...)  printf("[D][%s] " fmt "\r\n", HFSM_LOG_TAG, ##__VA_ARGS__)

#else

/* --- 情况三：局部开关关闭 -> 全部定义为空 (无代码生成) --- */
#define HFSM_LOGE(fmt, ...)
#define HFSM_LOGW(fmt, ...)
#define HFSM_LOGI(fmt, ...)
#define HFSM_LOGD(fmt, ...)

#endif
/**
 * @brief 初始化状态机
 * @param fsm 指向状态机实例的指针
 * @param initial_state 指向初始状态的指针
 */
void HFSM_Init(StateMachine *fsm, const State *initial_state) {
    if (fsm == NULL || initial_state == NULL) {
        HFSM_LOGI("HFSM_Init: Invalid parameters");
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
void HFSM_Transition(StateMachine *fsm, const State *new_state) {
    if (fsm == NULL || new_state == NULL) return;
    HFSM_LOGD("HFSM_Transition: Transitioning from %s to %s",
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
void HFSM_HandleEvent(StateMachine *fsm, const Event *event) {
    HFSM_LOGD("\n>>> Handling event: %d...", event->event_id);
    const State *s = fsm->current_state;

    while (s) {
        /* 1、判断当前是否有映射表 */
        if (s->event_actions) {
            /* 2、有就遍历该表并判断当前状态是否有可以被该事件触发的事项 */
            for (int i = 0; s->event_actions[i].handler != NULL; i++) {
                const EventAction_t *act = &s->event_actions[i];
                if (act->event_id == event->event_id) {
                    const bool handled = act->handler(fsm, event);
                    if (handled) {
                        HFSM_LOGI("Event %d handled by state: %s",
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


#endif
