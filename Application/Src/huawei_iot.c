#include "huawei_iot.h"

#include <stdio.h>
#include <string.h>

#include "hmac_sha256.h"
#include "lock_data.h"

static void bytes_to_hex(const uint8_t *in, size_t in_len, char *out, size_t out_sz)
{
    static const char *hex = "0123456789abcdef";
    if (!out || out_sz == 0) return;
    if (!in && in_len != 0) {
        out[0] = '\0';
        return;
    }
    if (out_sz < (in_len * 2u + 1u)) {
        out[0] = '\0';
        return;
    }
    for (size_t i = 0; i < in_len; i++) {
        out[i * 2u] = hex[(in[i] >> 4) & 0xFu];
        out[i * 2u + 1u] = hex[in[i] & 0xFu];
    }
    out[in_len * 2u] = '\0';
}

uint64_t huawei_iot_timestamp_ms(void)
{
    const uint64_t s = (uint64_t)lock_time_now_s();
    return s * 1000ull;
}

static bool build_auth_ts_utc_yyyymmddhh(uint32_t epoch_s_utc, char out[11])
{
    if (!out) return false;

    uint32_t days = epoch_s_utc / 86400u;
    const uint32_t sec_of_day = epoch_s_utc - days * 86400u;
    const uint32_t hour = sec_of_day / 3600u;

    uint32_t y = 1970u;
    for (;;) {
        const bool leap = ((y % 400u) == 0u) || (((y % 4u) == 0u) && ((y % 100u) != 0u));
        const uint32_t diy = leap ? 366u : 365u;
        if (days < diy) break;
        days -= diy;
        y++;
        if (y > 2099u) return false;
    }

    static const uint8_t mdays_norm[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    uint32_t m = 1u;
    for (uint32_t i = 0; i < 12u; i++) {
        uint32_t d = mdays_norm[i];
        const bool leap = ((y % 400u) == 0u) || (((y % 4u) == 0u) && ((y % 100u) != 0u));
        if (i == 1u && leap) d = 29u;
        if (days < d) {
            m = i + 1u;
            break;
        }
        days -= d;
    }

    const uint32_t day = days + 1u;
    const int n = snprintf(out, 11, "%04lu%02lu%02lu%02lu",
                           (unsigned long)y,
                           (unsigned long)m,
                           (unsigned long)day,
                           (unsigned long)hour);
    return (n == 10);
}

bool huawei_iot_build_mqtt_auth(uint32_t epoch_s_utc,
                                char *out_client_id,
                                size_t client_id_sz,
                                char *out_username,
                                size_t username_sz,
                                char *out_password,
                                size_t password_sz)
{
    if (!out_client_id || !out_username || !out_password) return false;
    if (client_id_sz == 0 || username_sz == 0 || password_sz == 0) return false;

    char ts[11];
    if (!build_auth_ts_utc_yyyymmddhh(epoch_s_utc, ts)) return false;

    const int n1 = snprintf(out_client_id, client_id_sz, "%s_0_%u_%s",
                            HUAWEI_IOT_DEVICE_ID,
                            (unsigned)HUAWEI_IOT_AUTH_SIGN_TYPE,
                            ts);
    const int n2 = snprintf(out_username, username_sz, "%s", HUAWEI_IOT_DEVICE_ID);
    if (n1 <= 0 || (size_t)n1 >= client_id_sz) return false;
    if (n2 <= 0 || (size_t)n2 >= username_sz) return false;

    /* IoTDA: Password = HMACSHA256(key=YYYYMMDDHH, msg=secret), output as hex */
    uint8_t mac[32];
    hmac_sha256((const uint8_t *)ts, strlen(ts),
                (const uint8_t *)HUAWEI_IOT_DEVICE_SECRET, strlen(HUAWEI_IOT_DEVICE_SECRET),
                mac);

    bytes_to_hex(mac, sizeof(mac), out_password, password_sz);
    return out_password[0] != '\0';
}

void huawei_iot_build_up_topic(char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return;
    (void)snprintf(out, out_sz, "$oc/devices/%s/sys/properties/report", HUAWEI_IOT_DEVICE_ID);
}

void huawei_iot_build_cmd_sub_topic(char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return;
    (void)snprintf(out, out_sz, "$oc/devices/%s/sys/commands/#", HUAWEI_IOT_DEVICE_ID);
}

void huawei_iot_build_user_door_event_topic(char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return;
    (void)snprintf(out, out_sz, "$oc/devices/%s/user/events/door", HUAWEI_IOT_DEVICE_ID);
}

void huawei_iot_build_user_cmd_topic(char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return;
    (void)snprintf(out, out_sz, "$oc/devices/%s/user/cmd", HUAWEI_IOT_DEVICE_ID);
}

void huawei_iot_build_user_cmd_ack_topic(char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return;
    (void)snprintf(out, out_sz, "$oc/devices/%s/user/cmd/ack", HUAWEI_IOT_DEVICE_ID);
}

bool huawei_iot_build_cmd_resp_topic_from_request(const char *request_topic, char *out, size_t out_sz)
{
    if (!request_topic || !out || out_sz == 0) return false;

    const char *p = strstr(request_topic, "/sys/commands/");
    if (!p) return false;
    p += strlen("/sys/commands/");

    const char *rid_end = strchr(p, '/');
    if (!rid_end) return false;
    const size_t rid_len = (size_t)(rid_end - p);
    if (rid_len == 0 || rid_len > 64u) return false;

    char rid[65];
    memcpy(rid, p, rid_len);
    rid[rid_len] = '\0';

    const int n = snprintf(out, out_sz, "$oc/devices/%s/sys/commands/%s/response", HUAWEI_IOT_DEVICE_ID, rid);
    return (n > 0) && ((size_t)n < out_sz);
}

static bool is_leap_year(uint32_t y)
{
    if ((y % 400u) == 0u) return true;
    if ((y % 100u) == 0u) return false;
    return (y % 4u) == 0u;
}

static uint32_t days_before_year(uint32_t y)
{
    uint32_t days = 0;
    for (uint32_t year = 1970u; year < y; year++) {
        days += is_leap_year(year) ? 366u : 365u;
    }
    return days;
}

static uint32_t days_before_month(uint32_t y, uint32_t m)
{
    static const uint8_t mdays_norm[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    uint32_t days = 0;
    for (uint32_t i = 1; i < m; i++) {
        uint32_t d = mdays_norm[i - 1u];
        if (i == 2u && is_leap_year(y)) d = 29u;
        days += d;
    }
    return days;
}

static int month_from_abbr(const char *m)
{
    if (!m) return -1;
    if (strncmp(m, "Jan", 3) == 0) return 1;
    if (strncmp(m, "Feb", 3) == 0) return 2;
    if (strncmp(m, "Mar", 3) == 0) return 3;
    if (strncmp(m, "Apr", 3) == 0) return 4;
    if (strncmp(m, "May", 3) == 0) return 5;
    if (strncmp(m, "Jun", 3) == 0) return 6;
    if (strncmp(m, "Jul", 3) == 0) return 7;
    if (strncmp(m, "Aug", 3) == 0) return 8;
    if (strncmp(m, "Sep", 3) == 0) return 9;
    if (strncmp(m, "Oct", 3) == 0) return 10;
    if (strncmp(m, "Nov", 3) == 0) return 11;
    if (strncmp(m, "Dec", 3) == 0) return 12;
    return -1;
}

bool huawei_iot_parse_sntp_time_to_epoch_s(const char *line, uint32_t *out_epoch_s)
{
    if (!line || !out_epoch_s) return false;

    const char *p = strstr(line, "+CIPSNTPTIME:");
    if (!p) return false;

    /*
     * Support both formats:
     * - +CIPSNTPTIME:"Thu Nov 05 23:02:10 2020"
     * - +CIPSNTPTIME:Thu Nov 05 23:02:10 2020
     *
     * Avoid sscanf here (newlib sscanf is stack-heavy and can trigger task stack overflow).
     */
    p += strlen("+CIPSNTPTIME:");
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\"') p++;

    char mon[4] = {0};
    uint32_t day = 0, hour = 0, min = 0, sec = 0, year = 0;

    const char *s = p;

    /* DOW */
    for (uint32_t i = 0; i < 3u; i++) {
        if (s[i] == '\0') return false;
    }
    s += 3;
    if (*s != ' ') return false;
    while (*s == ' ') s++;

    /* MON */
    for (uint32_t i = 0; i < 3u; i++) {
        if (s[i] == '\0') return false;
        mon[i] = s[i];
    }
    mon[3] = '\0';
    s += 3;
    if (*s != ' ') return false;
    while (*s == ' ') s++;

    /* DD (ESP firmware may pad with extra spaces) */
    if (*s < '0' || *s > '9') return false;
    while (*s >= '0' && *s <= '9') {
        day = day * 10u + (uint32_t)(*s - '0');
        s++;
    }
    if (*s != ' ') return false;
    while (*s == ' ') s++;

    /* HH:MM:SS */
    if (*s < '0' || *s > '9') return false;
    while (*s >= '0' && *s <= '9') {
        hour = hour * 10u + (uint32_t)(*s - '0');
        s++;
    }
    if (*s != ':') return false;
    s++;

    if (*s < '0' || *s > '9') return false;
    while (*s >= '0' && *s <= '9') {
        min = min * 10u + (uint32_t)(*s - '0');
        s++;
    }
    if (*s != ':') return false;
    s++;

    if (*s < '0' || *s > '9') return false;
    while (*s >= '0' && *s <= '9') {
        sec = sec * 10u + (uint32_t)(*s - '0');
        s++;
    }

    if (*s != ' ') return false;
    while (*s == ' ') s++;

    /* YYYY */
    if (*s < '0' || *s > '9') return false;
    while (*s >= '0' && *s <= '9') {
        year = year * 10u + (uint32_t)(*s - '0');
        s++;
    }

    const int month = month_from_abbr(mon);
    if (month < 1) return false;
    if (year < 1970u || year > 2099u) return false;
    if (day < 1u || day > 31u) return false;
    if (hour > 23u || min > 59u || sec > 59u) return false;

    const uint32_t y = year;
    const uint32_t m = (uint32_t)month;
    const uint32_t days = days_before_year(y) + days_before_month(y, m) + (day - 1u);
    const uint32_t epoch_s_local = days * 86400u + hour * 3600u + min * 60u + sec;

    /* After AT+CIPSNTPCFG=1,<tz>,... ESP AT returns local time in +CIPSNTPTIME.
 * Convert it back to UTC epoch for cloud auth. */
    {
        const int32_t tz_h = (int32_t)HUAWEI_IOT_TIMEZONE;
        int64_t utc = (int64_t)epoch_s_local - (int64_t)tz_h * 3600ll;
        if (utc < 0) utc = 0;
        *out_epoch_s = (uint32_t)utc;
    }
    return true;
}
