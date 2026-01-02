#include "rc522_my_test_task.h"
#include "cmsis_os2.h"
#include "log.h"
#include "rc522_my.h"

#include <string.h>

/* RC522（MFRC522）联调/自检任务：
 * - 周期性寻卡 -> 防冲突获取 UID -> 选卡
 * - 对 M1 卡：使用 KeyA 认证并读取/写入指定块（可用宏关闭写入）
 * - 用于验证 SPI/供电/天线与协议链路是否正常
 */
#define TLOG(fmt, ...) LOG_I("rc522_my_test", fmt, ##__VA_ARGS__)
#define RC522_MY_TEST_BLOCK 4u          /* 测试读写的块号（M1 卡） */
#define RC522_MY_TEST_ENABLE_WRITE 1    /* 1=写入测试块；0=只读 */

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

    /* 默认 KeyA（出厂常见） */
    const unsigned char key_a[6] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};
#if RC522_MY_TEST_ENABLE_WRITE
    /* 写入内容（16 字节，一个块） */
    const unsigned char write_data[16] = {
        'R', 'C', '5', '2', '2', '-', 'M', 'Y', '-', 'T', 'E', 'S', 'T', '-', '1', 0
    };
#endif

    unsigned char uid[6] = {0};
    unsigned char tag_type[2] = {0};
    unsigned char data[16] = {0};

    for (;;)
    {
        /* 1) 寻卡：若未检测到卡，循环等待 */
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

        /* 2) 防冲突：读取 UID */
        st = PcdAnticoll(uid);
        if (st != MI_OK)
        {
            TLOG("ANTICOLL failed st=%d", (int)st);
            osDelay(200);
            continue;
        }
        rc522_my_log_uid(uid);

        /* 3) 选卡 */
        st = PcdSelect(uid);
        if (st != MI_OK)
        {
            TLOG("SELECT failed st=%d", (int)st);
            osDelay(200);
            continue;
        }

        /* Ultralight 属于另一类协议流程，这里跳过认证/读写 */
        if (rc522_my_is_ultralight(tag_type))
        {
            TLOG("Ultralight tag, skip auth/read");
            osDelay(800);
            continue;
        }

        uid[4] = 0;
        uid[5] = 0;
        /* 4) M1 认证：KeyA + 块号 */
        st = PcdAuthState(PICC_AUTHENT1A, RC522_MY_TEST_BLOCK, (unsigned char *)key_a, uid);
        if (st != MI_OK)
        {
            TLOG("AUTH failed st=%d", (int)st);
            osDelay(800);
            continue;
        }

        /* 5) 读取块数据 */
        st = PcdRead(RC522_MY_TEST_BLOCK, data);
        if (st != MI_OK)
        {
            TLOG("READ failed st=%d", (int)st);
            osDelay(800);
            continue;
        }
        LOG_HEX("rc522_my_test", LOG_LEVEL_INFO, data, sizeof(data));

#if RC522_MY_TEST_ENABLE_WRITE
        /* 6) 写入并回读验证 */
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

        /* 7) 让卡进入 Halt，减少重复刷屏 */
        PcdHalt();
        osDelay(1000);
    }
}
