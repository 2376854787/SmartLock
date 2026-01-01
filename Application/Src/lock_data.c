#include "lock_data.h"

#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

static SemaphoreHandle_t s_mutex = NULL;
static lock_observer_t s_observer = NULL;
static void *s_observer_ctx = NULL;

static lock_rfid_entry_t s_rfid[LOCK_RFID_MAX];
static uint8_t s_rfid_count = 0;

static lock_pin_entry_t s_pins[LOCK_PIN_MAX];
static uint8_t s_pin_count = 0;
static uint32_t s_next_pin_id = 1;

static void notify(lock_evt_t evt)
{
    if (s_observer) {
        s_observer(evt, s_observer_ctx);
    }
}

uint32_t lock_time_now_s(void)
{
    return (uint32_t)(xTaskGetTickCount() / configTICK_RATE_HZ);
}

void lock_data_init(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
    }
}

void lock_data_set_observer(lock_observer_t cb, void *user_ctx)
{
    lock_data_init();
    s_observer = cb;
    s_observer_ctx = user_ctx;
}

/* ---------------- RFID ---------------- */

uint8_t lock_rfid_count(void)
{
    lock_data_init();
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return 0;
    uint8_t count = s_rfid_count;
    xSemaphoreGive(s_mutex);
    return count;
}

bool lock_rfid_get(uint8_t index, lock_rfid_entry_t *out_entry)
{
    if (!out_entry) return false;
    lock_data_init();
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return false;
    if (index >= s_rfid_count) {
        xSemaphoreGive(s_mutex);
        return false;
    }
    *out_entry = s_rfid[index];
    xSemaphoreGive(s_mutex);
    return true;
}

int lock_rfid_find_uid(const uint8_t uid[LOCK_RFID_UID_LEN])
{
    lock_data_init();
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return -1;
    for (uint8_t i = 0; i < s_rfid_count; i++) {
        if (memcmp(s_rfid[i].uid, uid, LOCK_RFID_UID_LEN) == 0) {
            xSemaphoreGive(s_mutex);
            return (int)i;
        }
    }
    xSemaphoreGive(s_mutex);
    return -1;
}

bool lock_rfid_add_uid(const uint8_t uid[LOCK_RFID_UID_LEN], const char *name_opt)
{
    if (!uid) return false;
    lock_data_init();
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return false;

    for (uint8_t i = 0; i < s_rfid_count; i++) {
        if (memcmp(s_rfid[i].uid, uid, LOCK_RFID_UID_LEN) == 0) {
            xSemaphoreGive(s_mutex);
            return true; /* already exists */
        }
    }

    if (s_rfid_count >= LOCK_RFID_MAX) {
        xSemaphoreGive(s_mutex);
        return false;
    }

    lock_rfid_entry_t *entry = &s_rfid[s_rfid_count++];
    memset(entry, 0, sizeof(*entry));
    memcpy(entry->uid, uid, LOCK_RFID_UID_LEN);
    entry->created_at_s = lock_time_now_s();
    entry->enabled = true;
    if (name_opt && name_opt[0]) {
        strncpy(entry->name, name_opt, sizeof(entry->name) - 1u);
    }

    xSemaphoreGive(s_mutex);
    notify(LOCK_EVT_RFID_CHANGED);
    return true;
}

bool lock_rfid_remove_uid(const uint8_t uid[LOCK_RFID_UID_LEN])
{
    if (!uid) return false;
    lock_data_init();
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return false;

    int index = -1;
    for (uint8_t i = 0; i < s_rfid_count; i++) {
        if (memcmp(s_rfid[i].uid, uid, LOCK_RFID_UID_LEN) == 0) {
            index = (int)i;
            break;
        }
    }
    if (index < 0) {
        xSemaphoreGive(s_mutex);
        return false;
    }

    for (uint8_t i = (uint8_t)index; (i + 1u) < s_rfid_count; i++) {
        s_rfid[i] = s_rfid[i + 1u];
    }
    s_rfid_count--;

    xSemaphoreGive(s_mutex);
    notify(LOCK_EVT_RFID_CHANGED);
    return true;
}

void lock_rfid_clear(void)
{
    lock_data_init();
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return;
    memset(s_rfid, 0, sizeof(s_rfid));
    s_rfid_count = 0;
    xSemaphoreGive(s_mutex);
    notify(LOCK_EVT_RFID_CHANGED);
}

/* ---------------- PIN / Password ---------------- */

static bool is_digits_only(const char *s)
{
    if (!s || !s[0]) return false;
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9') return false;
    }
    return true;
}

bool lock_pin_is_expired(const lock_pin_entry_t *entry, uint32_t now_s)
{
    if (!entry) return true;
    if (!entry->enabled) return true;
    if (entry->expires_at_s == 0) return false;
    return (now_s >= entry->expires_at_s);
}

uint8_t lock_pin_count(void)
{
    lock_data_init();
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return 0;
    uint8_t count = s_pin_count;
    xSemaphoreGive(s_mutex);
    return count;
}

bool lock_pin_get(uint8_t index, lock_pin_entry_t *out_entry)
{
    if (!out_entry) return false;
    lock_data_init();
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return false;
    if (index >= s_pin_count) {
        xSemaphoreGive(s_mutex);
        return false;
    }
    *out_entry = s_pins[index];
    xSemaphoreGive(s_mutex);
    return true;
}

bool lock_pin_add(const char *pin_digits, uint32_t ttl_minutes, const char *name_opt, uint32_t *out_id)
{
    if (!pin_digits) return false;
    uint32_t length = (uint32_t)strlen(pin_digits);
    if (length == 0 || length > LOCK_PIN_MAX_LEN) return false;
    if (!is_digits_only(pin_digits)) return false;

    lock_data_init();
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return false;

    for (uint8_t i = 0; i < s_pin_count; i++) {
        if (strcmp(s_pins[i].pin, pin_digits) == 0) {
            if (out_id) *out_id = s_pins[i].id;
            xSemaphoreGive(s_mutex);
            return true; /* already exists */
        }
    }

    if (s_pin_count >= LOCK_PIN_MAX) {
        xSemaphoreGive(s_mutex);
        return false;
    }

    lock_pin_entry_t *entry = &s_pins[s_pin_count++];
    memset(entry, 0, sizeof(*entry));
    entry->id = s_next_pin_id++;
    strncpy(entry->pin, pin_digits, sizeof(entry->pin) - 1u);
    entry->created_at_s = lock_time_now_s();
    entry->enabled = true;
    if (ttl_minutes > 0) {
        entry->expires_at_s = entry->created_at_s + (ttl_minutes * 60u);
    }
    if (name_opt && name_opt[0]) {
        strncpy(entry->name, name_opt, sizeof(entry->name) - 1u);
    }

    if (out_id) *out_id = entry->id;

    xSemaphoreGive(s_mutex);
    notify(LOCK_EVT_PIN_CHANGED);
    return true;
}

bool lock_pin_remove(uint32_t id)
{
    lock_data_init();
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return false;

    int index = -1;
    for (uint8_t i = 0; i < s_pin_count; i++) {
        if (s_pins[i].id == id) {
            index = (int)i;
            break;
        }
    }
    if (index < 0) {
        xSemaphoreGive(s_mutex);
        return false;
    }

    for (uint8_t i = (uint8_t)index; (i + 1u) < s_pin_count; i++) {
        s_pins[i] = s_pins[i + 1u];
    }
    s_pin_count--;

    xSemaphoreGive(s_mutex);
    notify(LOCK_EVT_PIN_CHANGED);
    return true;
}

void lock_pin_clear(void)
{
    lock_data_init();
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return;
    memset(s_pins, 0, sizeof(s_pins));
    s_pin_count = 0;
    xSemaphoreGive(s_mutex);
    notify(LOCK_EVT_PIN_CHANGED);
}

bool lock_pin_verify(const char *pin_digits, uint32_t *out_id)
{
    if (!pin_digits) return false;
    lock_data_init();
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return false;

    uint32_t now_s = lock_time_now_s();
    for (uint8_t i = 0; i < s_pin_count; i++) {
        if (strcmp(s_pins[i].pin, pin_digits) != 0) continue;
        if (lock_pin_is_expired(&s_pins[i], now_s)) {
            continue;
        }
        if (out_id) *out_id = s_pins[i].id;
        xSemaphoreGive(s_mutex);
        return true;
    }

    xSemaphoreGive(s_mutex);
    return false;
}

