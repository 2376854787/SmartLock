#include "rc522_my_test_task.h"
#include "cmsis_os2.h"
#include "log.h"
#include "rc522_my.h"

#include <string.h>

#define TLOG(fmt, ...) LOG_I("rc522_my_test", fmt, ##__VA_ARGS__)
#define RC522_MY_TEST_BLOCK 4u
#define RC522_MY_TEST_ENABLE_WRITE 1

static void rc522_my_log_uid(const unsigned char uid[4])
{
    TLOG("UID=%02X%02X%02X%02X", uid[0], uid[1], uid[2], uid[3]);
}

static int rc522_my_is_ultralight(const unsigned char tag_type[2])
{
    return (tag_type[0] == 0x44u) && (tag_type[1] == 0x00u);
}

void RC522_MyTestTask(void *argument)
{
    (void)argument;

    osDelay(500);
    RC522_Init();
    TLOG("VersionReg=0x%02X", ReadRawRC(VersionReg));

    const unsigned char key_a[6] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};
#if RC522_MY_TEST_ENABLE_WRITE
    const unsigned char write_data[16] = {
        'R', 'C', '5', '2', '2', '-', 'M', 'Y', '-', 'T', 'E', 'S', 'T', '-', '1', 0
    };
#endif

    unsigned char uid[6] = {0};
    unsigned char tag_type[2] = {0};
    unsigned char data[16] = {0};

    for (;;)
    {
        char st = PcdRequest(PICC_REQALL, tag_type);
        if (st != MI_OK)
        {
            if (st != MI_NOTAGERR)
            {
                TLOG("REQ failed st=%d", (int)st);
            }
            osDelay(200);
            continue;
        }

        st = PcdAnticoll(uid);
        if (st != MI_OK)
        {
            TLOG("ANTICOLL failed st=%d", (int)st);
            osDelay(200);
            continue;
        }
        rc522_my_log_uid(uid);

        st = PcdSelect(uid);
        if (st != MI_OK)
        {
            TLOG("SELECT failed st=%d", (int)st);
            osDelay(200);
            continue;
        }

        if (rc522_my_is_ultralight(tag_type))
        {
            TLOG("Ultralight tag, skip auth/read");
            osDelay(800);
            continue;
        }

        uid[4] = 0;
        uid[5] = 0;
        st = PcdAuthState(PICC_AUTHENT1A, RC522_MY_TEST_BLOCK, (unsigned char *)key_a, uid);
        if (st != MI_OK)
        {
            TLOG("AUTH failed st=%d", (int)st);
            osDelay(800);
            continue;
        }

        st = PcdRead(RC522_MY_TEST_BLOCK, data);
        if (st != MI_OK)
        {
            TLOG("READ failed st=%d", (int)st);
            osDelay(800);
            continue;
        }
        LOG_HEX("rc522_my_test", LOG_LEVEL_INFO, data, sizeof(data));

#if RC522_MY_TEST_ENABLE_WRITE
        st = PcdWrite(RC522_MY_TEST_BLOCK, (unsigned char *)write_data);
        if (st != MI_OK)
        {
            TLOG("WRITE failed st=%d", (int)st);
            osDelay(800);
            continue;
        }
        TLOG("WRITE OK, read back");
        memset(data, 0, sizeof(data));
        st = PcdRead(RC522_MY_TEST_BLOCK, data);
        if (st == MI_OK)
        {
            LOG_HEX("rc522_my_test", LOG_LEVEL_INFO, data, sizeof(data));
        }
        else
        {
            TLOG("READ back failed st=%d", (int)st);
        }
#endif

        PcdHalt();
        osDelay(1000);
    }
}
