#include "../Inc/as608_test_task.h"
#include "cmsis_os2.h"
#include "as608_service.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "as608_port.h"
#include "usart.h"

/* 你可以按工程实际替换为 LOGI/LOGE */
#define TLOG(fmt, ...)   printf("[AS608_TEST] " fmt "\r\n", ##__VA_ARGS__)

/* 可选：如果你有 LED 指示，可打开下面宏并改成你的 LED 引脚 */
// #define AS608_TEST_USE_LED 1
#ifdef AS608_TEST_USE_LED
#include "main.h"
static void test_led_toggle(void) { HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin); }
#else
static void test_led_toggle(void) {
    /* no-op */
}
#endif

/* 把 as608_status_t 转为可读字符串（方便排障） */
static const char *as608_status_str(as608_status_t st) {
    switch (st) {
        case AS608_STATUS_OK: return "OK";
        case AS608_STATUS_FRAME_ERROR: return "FRAME_ERROR";
        case AS608_STATUS_NO_FINGERPRINT: return "NO_FINGERPRINT";
        case AS608_STATUS_INPUT_ERROR: return "INPUT_ERROR";
        case AS608_STATUS_IMAGE_TOO_DRY: return "IMAGE_TOO_DRY";
        case AS608_STATUS_IMAGE_TOO_WET: return "IMAGE_TOO_WET";
        case AS608_STATUS_IMAGE_TOO_CLUTTER: return "IMAGE_TOO_CLUTTER";
        case AS608_STATUS_IMAGE_TOO_FEW_FEATURE: return "IMAGE_TOO_FEW_FEATURE";
        case AS608_STATUS_NOT_MATCH: return "NOT_MATCH";
        case AS608_STATUS_NOT_FOUND: return "NOT_FOUND";
        case AS608_STATUS_FEATURE_COMBINE_ERROR: return "FEATURE_COMBINE_ERROR";
        case AS608_STATUS_LIB_ADDR_OVER: return "LIB_ADDR_OVER";
        case AS608_STATUS_LIB_READ_ERROR: return "LIB_READ_ERROR";
        case AS608_STATUS_UPLOAD_FEATURE_ERROR: return "UPLOAD_FEATURE_ERROR";
        case AS608_STATUS_NO_FRAME: return "NO_FRAME";
        case AS608_STATUS_UPLOAD_IMAGE_ERROR: return "UPLOAD_IMAGE_ERROR";
        case AS608_STATUS_LIB_DELETE_ERROR: return "LIB_DELETE_ERROR";
        case AS608_STATUS_LIB_CLEAR_ERROR: return "LIB_CLEAR_ERROR";
        case AS608_STATUS_ENTER_LOW_POWER_ERROR: return "ENTER_LOW_POWER_ERROR";
        case AS608_STATUS_COMMAND_INVALID: return "COMMAND_INVALID";
        case AS608_STATUS_RESET_ERROR: return "RESET_ERROR";
        case AS608_STATUS_BUFFER_INVALID: return "BUFFER_INVALID";
        case AS608_STATUS_UPDATE_ERROR: return "UPDATE_ERROR";
        case AS608_STATUS_NO_MOVE: return "NO_MOVE";
        case AS608_STATUS_FLASH_ERROR: return "FLASH_ERROR";
        case AS608_STATUS_F0_RESPONSE: return "F0_RESPONSE";
        case AS608_STATUS_F1_RESPONSE: return "F1_RESPONSE";
        case AS608_STATUS_FLASH_WRITE_SUM_ERROR: return "FLASH_WRITE_SUM_ERROR";
        case AS608_STATUS_FLASH_WRITE_HEADER_ERROR: return "FLASH_WRITE_HEADER_ERROR";
        case AS608_STATUS_FLASH_WRITE_LENGTH_ERROR: return "FLASH_WRITE_LENGTH_ERROR";
        case AS608_STATUS_FLASH_WRITE_LENGTH_TOO_LONG: return "FLASH_WRITE_LENGTH_TOO_LONG";
        case AS608_STATUS_FLASH_WRITE_ERROR: return "FLASH_WRITE_ERROR";
        case AS608_STATUS_UNKNOWN: return "UNKNOWN";
        case AS608_STATUS_REG_INVALID: return "REG_INVALID";
        case AS608_STATUS_DATA_INVALID: return "DATA_INVALID";
        case AS608_STATUS_NOTE_PAGE_INVALID: return "NOTE_PAGE_INVALID";
        case AS608_STATUS_PORT_INVALID: return "PORT_INVALID";
        case AS608_STATUS_ENROOL_ERROR: return "ENROOL_ERROR";
        case AS608_STATUS_LIB_FULL: return "LIB_FULL";
        default: return "UNDEF";
    }
}

typedef enum {
    STEP_BOOT = 0,
    STEP_INDEX_TABLE, /* 不需要手指：验证链路 */
    STEP_ENROLL_ID1, /* Create */
    STEP_VERIFY_ID1, /* Read/Search */
    STEP_DELETE_ID1, /* Delete */
    STEP_VERIFY_NOT_FOUND, /* 再次 Read/Search：期望找不到 */
    STEP_DONE_WAIT,
} test_step_t;

void AS608_TestTask(void *argument) {
    (void) argument;

    /* 建议系统启动后稍等一会儿，避免别的初始化抢占 UART */
    osDelay(800);

    TLOG("task start.");

    test_step_t step = STEP_BOOT;

    /* 参数可按需调整 */
    const uint16_t test_id = 1;
    const uint32_t enroll_timeout_ms = 20000; /* 录入时给足时间 */
    const uint32_t search_timeout_ms = 8000;
    /* 初始化as608服务 */
    AS608_Port_BindUart(&huart4);
    AS608_Service_Init(0xFFFFFFFF, 0x00000000);
    for (;;) {
        test_led_toggle();

        switch (step) {
            case STEP_BOOT:
                TLOG("BOOT -> start smoke test.");
                step = STEP_INDEX_TABLE;
                break;

            case STEP_INDEX_TABLE: {
                uint8_t table[32];
                memset(table, 0, sizeof(table));

                /* 注意：你的 enum 没有 AS608_STATUS_ERROR，所以用 UNKNOWN 初始化 */
                as608_status_t st = AS608_STATUS_UNKNOWN;

                TLOG("Step1: Read index table (no finger needed)...");
                as608_svc_rc_t rc = AS608_List_IndexTable(0, table, &st);

                if (rc == AS608_SVC_OK && st == AS608_STATUS_OK) {
                    TLOG("IndexTable OK. first16:");
                    for (int i = 0; i < 16; i++)
                        printf("%02X ", table[i]);
                    printf("\r\n");

                    step = STEP_ENROLL_ID1;
                } else {
                    TLOG("IndexTable FAIL. rc=%d st=0x%02X(%s)", (int)rc, (unsigned)st, as608_status_str(st));
                    TLOG("=> 这一步失败：优先怀疑 UART RX 没通(回调没进/没转发)、线序错、未共地、波特率不对");
                    osDelay(1500);
                }
                break;
            }

            case STEP_ENROLL_ID1: {
                as608_status_t st = AS608_STATUS_UNKNOWN;

                TLOG("Step2(Create): Enroll ID=%u", test_id);
                TLOG("=> 通常需要同一手指按两次：按下->抬起->再按下");

                as608_svc_rc_t rc = AS608_CRUD_Create(test_id, enroll_timeout_ms, &st);

                if (rc == AS608_SVC_OK && st == AS608_STATUS_OK) {
                    TLOG("Enroll OK. (ID=%u)", test_id);
                    step = STEP_VERIFY_ID1;
                } else {
                    TLOG("Enroll FAIL. rc=%d st=0x%02X(%s)", (int)rc, (unsigned)st, as608_status_str(st));
                    osDelay(1500);
                }
                break;
            }

            case STEP_VERIFY_ID1: {
                uint16_t id = 0;
                uint16_t score = 0;
                as608_status_t st = AS608_STATUS_UNKNOWN;

                TLOG("Step3(Read/Search): Verify finger, expect match ID=%u", test_id);
                TLOG("=> 请按刚才录入的同一手指");

                as608_svc_rc_t rc = AS608_CRUD_Read(search_timeout_ms, &id, &score, &st);

                if (rc == AS608_SVC_OK && st == AS608_STATUS_OK) {
                    TLOG("Verify OK. matched id=%u score=%u", id, score);
                    step = STEP_DELETE_ID1;
                } else if (rc == AS608_SVC_OK && st == AS608_STATUS_NOT_FOUND) {
                    TLOG("Verify NOT_FOUND. (finger ok but not matched) => 可能不是同一手指/录入质量差");
                    osDelay(1200);
                } else {
                    TLOG("Verify FAIL. rc=%d st=0x%02X(%s)", (int)rc, (unsigned)st, as608_status_str(st));
                    osDelay(1200);
                }
                break;
            }

            case STEP_DELETE_ID1: {
                as608_status_t st = AS608_STATUS_UNKNOWN;

                TLOG("Step4(Delete): Delete ID=%u", test_id);
                as608_svc_rc_t rc = AS608_CRUD_Delete(test_id, &st);

                if (rc == AS608_SVC_OK && st == AS608_STATUS_OK) {
                    TLOG("Delete OK. ID=%u", test_id);
                    step = STEP_VERIFY_NOT_FOUND;
                } else {
                    TLOG("Delete FAIL. rc=%d st=0x%02X(%s)", (int)rc, (unsigned)st, as608_status_str(st));
                    osDelay(1200);
                }
                break;
            }

            case STEP_VERIFY_NOT_FOUND: {
                uint16_t id = 0;
                uint16_t score = 0;
                as608_status_t st = AS608_STATUS_UNKNOWN;

                TLOG("Step5(Read/Search): Verify again, expect NOT_FOUND (because deleted).");
                TLOG("=> 仍按刚才那根手指");

                as608_svc_rc_t rc = AS608_CRUD_Read(search_timeout_ms, &id, &score, &st);

                if (rc == AS608_SVC_OK && st == AS608_STATUS_NOT_FOUND) {
                    TLOG("Verify NOT_FOUND OK. CRUD loop passed.");
                    step = STEP_DONE_WAIT;
                } else if (rc == AS608_SVC_OK && st == AS608_STATUS_OK) {
                    TLOG("Unexpected MATCH: id=%u score=%u => 删除未生效/库里还有相同指纹", id, score);
                    step = STEP_DONE_WAIT;
                } else {
                    TLOG("Verify FAIL. rc=%d st=0x%02X(%s)", (int)rc, (unsigned)st, as608_status_str(st));
                    osDelay(1200);
                }
                break;
            }

            case STEP_DONE_WAIT:
            default:
                TLOG("DONE. wait 5s then restart loop.");
                osDelay(5000);
                step = STEP_INDEX_TABLE;
                break;
        }

        osDelay(50);
    }
}
