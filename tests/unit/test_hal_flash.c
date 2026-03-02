#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "hal_flash.h"
#include "hal_flash_port.h"
#include "osal.h"

static hal_flash_port_evt_cb_t g_port_evt_cb = NULL;
static void *g_port_evt_user                 = NULL;
static uint8_t g_flash_mem[256];

void OSAL_enter_critical_ex(osal_crit_state_t *state) {
    if (state != NULL) *state = 0u;
}

void OSAL_exit_critical_ex(osal_crit_state_t state) {
    (void)state;
}

void OSAL_enter_critical_from_isr(osal_crit_state_t *state) {
    if (state != NULL) *state = 0u;
}

void OSAL_exit_critical_from_isr(osal_crit_state_t state) {
    (void)state;
}

bool OSAL_kernel_is_running(void) {
    return false;
}

ret_code_t OSAL_mutex_create(osal_mutex_t *out, const char *name, bool recursive, bool prio_inherit) {
    (void)out;
    (void)name;
    (void)recursive;
    (void)prio_inherit;
    return RET_OK;
}

ret_code_t OSAL_mutex_delete(osal_mutex_t mutex) {
    (void)mutex;
    return RET_OK;
}

ret_code_t OSAL_mutex_lock(osal_mutex_t mutex, uint32_t timeout_ms) {
    (void)mutex;
    (void)timeout_ms;
    return RET_OK;
}

ret_code_t OSAL_mutex_unlock(osal_mutex_t mutex) {
    (void)mutex;
    return RET_OK;
}

ret_code_t OSAL_sem_create(osal_sem_t *out, const char *name, uint32_t initial_count,
                           uint32_t max_count) {
    (void)out;
    (void)name;
    (void)initial_count;
    (void)max_count;
    return RET_OK;
}

ret_code_t OSAL_sem_delete(osal_sem_t sem) {
    (void)sem;
    return RET_OK;
}

ret_code_t OSAL_sem_take(osal_sem_t sem, uint32_t timeout_ms) {
    (void)sem;
    (void)timeout_ms;
    return RET_OK;
}

ret_code_t OSAL_sem_give(osal_sem_t sem) {
    (void)sem;
    return RET_OK;
}

ret_code_t OSAL_sem_give_from_isr(osal_sem_t sem) {
    (void)sem;
    return RET_OK;
}

ret_code_t OSAL_delay_ms(uint32_t delay_ms) {
    (void)delay_ms;
    return RET_OK;
}

ret_code_t OSAL_msgq_put(osal_msgq_t queue, void *item, uint32_t timeout_ms) {
    (void)queue;
    (void)item;
    (void)timeout_ms;
    return RET_OK;
}

ret_code_t OSAL_thread_flags_set(osal_thread_t thread, osal_flags_t flags) {
    (void)thread;
    (void)flags;
    return RET_OK;
}

ret_code_t OSAL_thread_create(osal_thread_t *out, void (*entry)(void *), void *arg,
                              const osal_thread_attr_t *attr) {
    (void)out;
    (void)entry;
    (void)arg;
    (void)attr;
    return RET_OK;
}

osal_flags_t OSAL_thread_flags_wait(osal_flags_t flags, uint32_t options, uint32_t timeout_ms) {
    (void)flags;
    (void)options;
    (void)timeout_ms;
    return 0u;
}

ret_code_t hal_flash_port_get_info(hal_flash_info_t *out) {
    if (out == NULL) return RET_MAKE_PARAM(RET_MOD_PORT, RET_SUB_PORT_FLASH, RET_R_NULL_PTR);
    *out = (hal_flash_info_t){
        .base = 0x08000000u,
        .total_size = sizeof(g_flash_mem),
        .prog_unit = 4u,
        .erase_value = 0xFFu,
        .min_erase_size = 16u,
        .require_erase_before_write = true,
    };
    return RET_OK;
}

ret_code_t hal_flash_port_get_region(uint32_t addr, hal_flash_region_t *out) {
    if ((out == NULL) || (addr < 0x08000000u) || (addr >= 0x08000000u + sizeof(g_flash_mem))) {
        return RET_MAKE_PARAM(RET_MOD_PORT, RET_SUB_PORT_FLASH, RET_R_INVALID_ARG);
    }
    *out = (hal_flash_region_t){.index = 0u, .addr = 0x08000000u, .size = sizeof(g_flash_mem)};
    return RET_OK;
}

ret_code_t hal_flash_port_read(uint32_t addr, void *buf, uint32_t len) {
    if ((buf == NULL) || (len == 0u)) return RET_MAKE_PARAM(RET_MOD_PORT, RET_SUB_PORT_FLASH, RET_R_INVALID_ARG);
    memcpy(buf, &g_flash_mem[addr - 0x08000000u], len);
    return RET_OK;
}

ret_code_t hal_flash_port_erase(uint32_t addr, uint32_t len) {
    memset(&g_flash_mem[addr - 0x08000000u], 0xFF, len);
    return RET_OK;
}

ret_code_t hal_flash_port_write(uint32_t addr, const void *data, uint32_t len) {
    memcpy(&g_flash_mem[addr - 0x08000000u], data, len);
    return RET_OK;
}

ret_code_t hal_flash_port_blank_check(uint32_t addr, uint32_t len, bool *out) {
    uint32_t i = 0u;
    if (out == NULL) return RET_MAKE_PARAM(RET_MOD_PORT, RET_SUB_PORT_FLASH, RET_R_NULL_PTR);
    *out = true;
    for (i = 0u; i < len; ++i) {
        if (g_flash_mem[(addr - 0x08000000u) + i] != 0xFFu) {
            *out = false;
            break;
        }
    }
    return RET_OK;
}

ret_code_t hal_flash_port_set_evt_cb(hal_flash_port_evt_cb_t cb, void *user) {
    g_port_evt_cb   = cb;
    g_port_evt_user = user;
    return RET_OK;
}

ret_code_t hal_flash_port_erase_it(uint32_t addr, uint32_t len) {
    (void)hal_flash_port_erase(addr, len);
    g_port_evt_cb(g_port_evt_user, HAL_FLASH_PORT_EVT_DONE);
    return RET_OK;
}

ret_code_t hal_flash_port_write_it(uint32_t addr, const void *data, uint32_t len) {
    (void)hal_flash_port_write(addr, data, len);
    g_port_evt_cb(g_port_evt_user, HAL_FLASH_PORT_EVT_DONE);
    return RET_OK;
}

int main(void) {
    hal_flash_status_t status          = HAL_FLASH_STATUS_UNINIT;
    hal_flash_job_result_t job_result  = HAL_FLASH_JOB_NONE;
    hal_flash_info_t info              = {0};
    hal_flash_region_t region          = {0};
    bool is_blank                      = false;
    uint8_t read_back[4]               = {0};
    const uint8_t data[4]              = {1u, 2u, 3u, 4u};
    const hal_flash_cfg_t cfg = {
        .job_end_notify = NULL,
        .job_error_notify = NULL,
        .user = NULL,
    };

    memset(g_flash_mem, 0xFF, sizeof(g_flash_mem));

    assert(ret_is_ok(hal_flash_init(&cfg)));
    assert(ret_is_ok(hal_flash_get_status(&status)));
    assert(status == HAL_FLASH_STATUS_IDLE);
    assert(ret_is_ok(hal_flash_get_job_result(&job_result)));
    assert(job_result == HAL_FLASH_JOB_NONE);
    assert(ret_is_ok(hal_flash_get_info(&info)));
    assert(info.erase_value == 0xFFu);
    assert(ret_is_ok(hal_flash_get_region(0x08000000u, &region)));
    assert(region.size == sizeof(g_flash_mem));

    assert(ret_is_ok(hal_flash_write_sync(0x08000010u, data, sizeof(data))));
    assert(ret_is_ok(hal_flash_read_sync(0x08000010u, read_back, sizeof(read_back))));
    assert(memcmp(data, read_back, sizeof(data)) == 0);
    assert(ret_is_ok(hal_flash_compare_sync(0x08000010u, data, sizeof(data))));
    memset(read_back, 0, sizeof(read_back));
    assert(ret_is_ok(hal_flash_read(0x08000010u, read_back, sizeof(read_back))));
    assert(ret_is_ok(hal_flash_get_status(&status)));
    assert(status == HAL_FLASH_STATUS_BUSY);
    hal_flash_main_function();
    assert(ret_is_ok(hal_flash_get_status(&status)));
    assert(status == HAL_FLASH_STATUS_IDLE);
    assert(memcmp(data, read_back, sizeof(data)) == 0);
    assert(ret_is_ok(hal_flash_blank_check_sync(0x08000000u, 8u, &is_blank)));
    assert(is_blank);
    assert(ret_is_ok(hal_flash_is_erased(0x08000000u, 8u, &is_blank)));
    assert(is_blank);
    assert(ret_is_ok(hal_flash_erase_sync(0x08000010u, sizeof(data))));
    assert(ret_is_ok(hal_flash_blank_check_sync(0x08000010u, sizeof(data), &is_blank)));
    assert(is_blank);

    puts("test_hal_flash: PASS");
    return 0;
}
