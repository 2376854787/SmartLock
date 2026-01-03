#include "lock_devices.h"

#include "FreeRTOS.h"
#include "event_groups.h"
#include "task.h"

#include "as608_port.h"
#include "as608_service.h"
#include "log.h"
#include "rc522_my.h"
#include "usart.h"

#define DEV_EVT_AS608_READY (1u << 0)
#define DEV_EVT_AS608_FAIL  (1u << 1)
#define DEV_EVT_RC522_READY (1u << 2)
#define DEV_EVT_RC522_FAIL  (1u << 3)

static EventGroupHandle_t s_dev_evt = NULL;
static TaskHandle_t s_init_task = NULL;

static void dev_init_task(void *arg)
{
    (void)arg;

    if (s_dev_evt == NULL) {
        vTaskDelete(NULL);
        return;
    }

    xEventGroupClearBits(
        s_dev_evt,
        DEV_EVT_AS608_READY | DEV_EVT_AS608_FAIL | DEV_EVT_RC522_READY | DEV_EVT_RC522_FAIL);

    LOG_I("DEV_INIT", "init start");

    AS608_Port_BindUart(&huart4);
    as608_svc_rc_t as608_rc = AS608_Service_Init(0xFFFFFFFFu, 0x00000000u);
    if (as608_rc == AS608_SVC_OK) {
        xEventGroupSetBits(s_dev_evt, DEV_EVT_AS608_READY);
        LOG_I("DEV_INIT", "AS608 ready, capacity=%u", (unsigned)AS608_Get_Capacity());
    } else {
        xEventGroupSetBits(s_dev_evt, DEV_EVT_AS608_FAIL);
        LOG_E("DEV_INIT", "AS608 init failed rc=%d", (int)as608_rc);
    }

    RC522_Init();
    uint8_t ver = (uint8_t)ReadRawRC(VersionReg);
    if (ver == 0x00u || ver == 0xFFu) {
        xEventGroupSetBits(s_dev_evt, DEV_EVT_RC522_FAIL);
        LOG_E("DEV_INIT", "RC522 init failed (VersionReg=0x%02X)", (unsigned)ver);
    } else {
        xEventGroupSetBits(s_dev_evt, DEV_EVT_RC522_READY);
        LOG_I("DEV_INIT", "RC522 ready (VersionReg=0x%02X)", (unsigned)ver);
    }

    LOG_I("DEV_INIT", "init done");
    s_init_task = NULL;
    vTaskDelete(NULL);
}

void LockDevices_Start(void)
{
    if (s_dev_evt == NULL) {
        s_dev_evt = xEventGroupCreate();
        if (s_dev_evt == NULL) {
            return;
        }
    }

    if (s_init_task != NULL) {
        return;
    }

    (void)xTaskCreate(dev_init_task, "dev_init", 768, NULL, (tskIDLE_PRIORITY + 3), &s_init_task);
}

static bool wait_ready(EventBits_t ready_bit, EventBits_t fail_bit, uint32_t timeout_ms)
{
    if (s_dev_evt == NULL) return false;

    TickType_t ticks = (timeout_ms == 0u) ? 0u : pdMS_TO_TICKS(timeout_ms);
    EventBits_t bits = xEventGroupWaitBits(
        s_dev_evt,
        ready_bit | fail_bit,
        pdFALSE,
        pdFALSE,
        (timeout_ms == 0xFFFFFFFFu) ? portMAX_DELAY : ticks);

    return (bits & ready_bit) != 0u;
}

bool LockDevices_As608Ready(void)
{
    if (s_dev_evt == NULL) return false;
    return (xEventGroupGetBits(s_dev_evt) & DEV_EVT_AS608_READY) != 0u;
}

bool LockDevices_Rc522Ready(void)
{
    if (s_dev_evt == NULL) return false;
    return (xEventGroupGetBits(s_dev_evt) & DEV_EVT_RC522_READY) != 0u;
}

bool LockDevices_WaitAs608Ready(uint32_t timeout_ms)
{
    return wait_ready(DEV_EVT_AS608_READY, DEV_EVT_AS608_FAIL, timeout_ms);
}

bool LockDevices_WaitRc522Ready(uint32_t timeout_ms)
{
    return wait_ready(DEV_EVT_RC522_READY, DEV_EVT_RC522_FAIL, timeout_ms);
}

