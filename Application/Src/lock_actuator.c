#include "lock_actuator.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "log.h"
#include "mg90s.h"

typedef enum
{
    LOCK_ACT_CMD_LOCK = 0,
    LOCK_ACT_CMD_UNLOCK,
} lock_act_cmd_t;

typedef struct
{
    lock_act_cmd_t cmd;
    uint32_t hold_ms;
} lock_act_msg_t;

#ifndef LOCK_ACTUATOR_QUEUE_LEN
#define LOCK_ACTUATOR_QUEUE_LEN 4u
#endif

#ifndef LOCK_ACTUATOR_TASK_STACK_WORDS
#define LOCK_ACTUATOR_TASK_STACK_WORDS 512u
#endif

static QueueHandle_t s_q = NULL;
static TaskHandle_t s_task = NULL;
static volatile bool s_ready = false;

#if (configSUPPORT_STATIC_ALLOCATION == 1)
static StaticQueue_t s_q_struct;
static uint8_t s_q_storage[LOCK_ACTUATOR_QUEUE_LEN * sizeof(lock_act_msg_t)];
static StaticTask_t s_task_tcb;
static StackType_t s_task_stack[LOCK_ACTUATOR_TASK_STACK_WORDS];
#endif

static void lock_act_task(void *arg)
{
    (void)arg;

    LOG_I("ACT", "init servo");
    mg90s_init();
    mg90s_lock();
    s_ready = true;
    LOG_I("ACT", "ready");

    bool unlocked = false;
    TickType_t unlock_deadline = 0;

    for (;;)
    {
        TickType_t wait_ticks = portMAX_DELAY;
        if (unlocked)
        {
            TickType_t now = xTaskGetTickCount();
            if ((int32_t)(unlock_deadline - now) <= 0)
            {
                mg90s_lock();
                unlocked = false;
                continue;
            }
            wait_ticks = unlock_deadline - now;
        }

        lock_act_msg_t msg;
        if (xQueueReceive(s_q, &msg, wait_ticks) == pdTRUE)
        {
            if (msg.cmd == LOCK_ACT_CMD_LOCK)
            {
                mg90s_lock();
                unlocked = false;
                continue;
            }

            uint32_t hold_ms = msg.hold_ms ? msg.hold_ms : (uint32_t)LOCK_ACTUATOR_UNLOCK_HOLD_MS;
            if (hold_ms == 0u)
            {
                hold_ms = 1u;
            }

            mg90s_unlock();
            unlocked = true;
            unlock_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(hold_ms);
            continue;
        }

        /* timeout -> auto lock */
        if (unlocked)
        {
            mg90s_lock();
            unlocked = false;
        }
    }
}

void LockActuator_Start(void)
{
    if (s_task != NULL)
    {
        return;
    }

    s_ready = false;

#if (configSUPPORT_STATIC_ALLOCATION == 1)
    if (s_q == NULL)
    {
        s_q = xQueueCreateStatic(
            LOCK_ACTUATOR_QUEUE_LEN,
            sizeof(lock_act_msg_t),
            s_q_storage,
            &s_q_struct);
    }
    if (s_q == NULL)
    {
        return;
    }

    s_task = xTaskCreateStatic(
        lock_act_task,
        "lock_act",
        LOCK_ACTUATOR_TASK_STACK_WORDS,
        NULL,
        (tskIDLE_PRIORITY + 2),
        s_task_stack,
        &s_task_tcb);
#else
    if (s_q == NULL)
    {
        s_q = xQueueCreate(LOCK_ACTUATOR_QUEUE_LEN, sizeof(lock_act_msg_t));
    }
    if (s_q == NULL)
    {
        return;
    }

    (void)xTaskCreate(lock_act_task, "lock_act", LOCK_ACTUATOR_TASK_STACK_WORDS, NULL, (tskIDLE_PRIORITY + 2), &s_task);
#endif
}

bool LockActuator_IsReady(void)
{
    return s_ready;
}

static bool send_cmd(lock_act_cmd_t cmd, uint32_t hold_ms)
{
    if (s_q == NULL) return false;
    lock_act_msg_t msg = {.cmd = cmd, .hold_ms = hold_ms};
    return xQueueSend(s_q, &msg, 0) == pdTRUE;
}

bool LockActuator_LockAsync(void)
{
    return send_cmd(LOCK_ACT_CMD_LOCK, 0);
}

bool LockActuator_UnlockAsync(void)
{
    return send_cmd(LOCK_ACT_CMD_UNLOCK, (uint32_t)LOCK_ACTUATOR_UNLOCK_HOLD_MS);
}

bool LockActuator_UnlockForAsync(uint32_t hold_ms)
{
    return send_cmd(LOCK_ACT_CMD_UNLOCK, hold_ms);
}

