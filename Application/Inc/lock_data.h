#ifndef LOCK_DATA_H
#define LOCK_DATA_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 凭据/权限数据存储（开发阶段：仅内存）。
 *
 * 目的：
 * - 统一管理 RFID/PIN 等凭据的 CRUD，让 UI 与后续 MQTT/云端逻辑复用同一份数据模型。
 * - 抽象存储后端：当前仅 RAM；后续可替换为 Flash/NVS，并支持云端同步。
 *
 * 线程安全：
 * - 接口可在多个 FreeRTOS 任务中调用（内部使用互斥锁保护）。
 */

typedef enum {
    LOCK_EVT_RFID_CHANGED = 1,
    LOCK_EVT_PIN_CHANGED = 2,
} lock_evt_t;

typedef void (*lock_observer_t)(lock_evt_t evt, void *user_ctx);

void lock_data_init(void);
void lock_data_set_observer(lock_observer_t cb, void *user_ctx);

/* 时间基准：未校时前为“上电后秒数”；SNTP/RTC 校时后可切换为 epoch 秒。 */
uint32_t lock_time_now_s(void);
void lock_time_set_epoch_s(uint32_t epoch_s);
bool lock_time_has_epoch(void);

/* ---------------- RFID ---------------- */

#define LOCK_RFID_UID_LEN 4u
#define LOCK_RFID_NAME_MAX 16u
#define LOCK_RFID_MAX 32u

typedef struct {
    uint8_t uid[LOCK_RFID_UID_LEN];
    uint32_t created_at_s;
    bool enabled;
    char name[LOCK_RFID_NAME_MAX];
} lock_rfid_entry_t;

uint8_t lock_rfid_count(void);
bool lock_rfid_get(uint8_t index, lock_rfid_entry_t *out_entry);
int lock_rfid_find_uid(const uint8_t uid[LOCK_RFID_UID_LEN]);
bool lock_rfid_add_uid(const uint8_t uid[LOCK_RFID_UID_LEN], const char *name_opt);
bool lock_rfid_remove_uid(const uint8_t uid[LOCK_RFID_UID_LEN]);
void lock_rfid_clear(void);

/* ---------------- PIN / 密码 ---------------- */

#define LOCK_PIN_MAX 10u
#define LOCK_PIN_MAX_LEN 6u
#define LOCK_PIN_NAME_MAX 16u

typedef struct {
    uint32_t id;                 /* stable ID for CRUD/MQTT */
    char pin[LOCK_PIN_MAX_LEN + 1u];
    uint32_t created_at_s;
    uint32_t expires_at_s;       /* 0 = never */
    bool enabled;
    char name[LOCK_PIN_NAME_MAX];
} lock_pin_entry_t;

uint8_t lock_pin_count(void);
bool lock_pin_get(uint8_t index, lock_pin_entry_t *out_entry);
bool lock_pin_add(const char *pin_digits, uint32_t ttl_minutes, const char *name_opt, uint32_t *out_id);
bool lock_pin_remove(uint32_t id);
void lock_pin_clear(void);
bool lock_pin_verify(const char *pin_digits, uint32_t *out_id);

bool lock_pin_is_expired(const lock_pin_entry_t *entry, uint32_t now_s);

#ifdef __cplusplus
}
#endif

#endif /* LOCK_DATA_H */

