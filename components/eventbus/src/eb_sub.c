#include "eb_sub.h"

#include <string.h>

#include "compiler_cus.h"
#include "eb_config.h"
#include "eb_eventdef.h"
#include "eb_freeze.h"
#include "eb_port.h"
#include "osal.h"
#ifndef EB_MAX_EVENTS
#define EB_MAX_EVENTS 128u
#endif

/* =====================================================================================
 * 订阅表实现：
 * - 当 EB_CFG_STATIC_FREEZE==1：运行期只读（推荐），用单表 + （可选）短临界区。
 *   优点：无原子依赖、无双表拷贝、最小 RAM/复杂度；符合 SOP “Freeze 后只读”。
 * - 当 EB_CFG_STATIC_FREEZE==0：支持运行期动态订阅（不推荐用于 Top-Tier），用 COW 双表。
 */

#if (EB_CFG_STATIC_FREEZE == 1)
/* 事件订阅者桶 */
typedef struct {
    eb_sub_t subs[EB_MAX_SUBS]; /* 同一事件订阅者配置信息 */
    uint16_t n;                 /* 订阅者个数 */
    uint16_t _pad;              /* 填充 */
} eb_bucket_t;
/* 所有的事件订阅者信息 */
static eb_bucket_t g_tbl[EB_MAX_EVENTS];
/**
 * @brief 初始化订阅表初始为0
 */
void eb_sub_init(void) {
    memset(g_tbl, 0, sizeof(g_tbl));
}
/**
 * @brief 根据配置使用 哈希表或者线性查找 返回事件在策略表的索引 即桶的索引
 * @param event_id
 * @return 返回索引
 */
static inline int32_t idx_of(uint32_t event_id) {
    return eb_event_index(event_id);
}
/**
 * @brief 两个订阅信息是否相等
 * @param a 订阅信息 a
 * @param b 订阅信息 b
 * @return 是否相等
 */
static bool sub_equal(const eb_sub_t* a, const eb_sub_t* b) {
    return (a->event_id == b->event_id) && (a->delivery == b->delivery) && (a->cb == b->cb) &&
           (a->mailbox == b->mailbox) && (a->user == b->user) && (a->key_mask == b->key_mask) &&
           (a->key_value == b->key_value) && (a->expected_type_tag == b->expected_type_tag);
}
/**
 * @brief 在订阅表里添加一个 事件
 * @param s 订阅者提供的订阅事件相关信息
 * @return 32位状态码
 */
eb_ret_t eb_sub_add(const eb_sub_t* s) {
    if (!s) return EB_ERR_BADARG;
    if (eb_is_frozen()) return EB_ERR_BADSTATE;
    /* 获取在策略表的索引 */
    const int32_t idx = idx_of(s->event_id);
    if (idx < 0 || idx >= (int32_t)EB_MAX_EVENTS) return EB_ERR_BADARG;
    uint32_t pm;
    eb_port_enter_critical(&pm);
    /* 将事件索引也映射过来 */
    eb_bucket_t* b = &g_tbl[(uint32_t)idx];
    /* 检查订阅者桶是否超过限制 */
    if (b->n >= (uint16_t)EB_MAX_SUBS) {
        eb_port_exit_critical(pm);
        // TODO 加上断言
        return EB_ERR_FULL;
    }

    /* 同一订阅重复注册，直接返回 OK */
    for (uint16_t i = 0; i < b->n; i++) {
        if (sub_equal(&b->subs[i], s)) {
            eb_port_exit_critical(pm);
            return EB_OK;
        }
    }
    /* 订阅者数 +1 并且将订阅者信息加入一个事件的列表*/
    b->subs[b->n++] = *s;
    eb_port_exit_critical(pm);
    return EB_OK;
}
/**
 * @brief 在订阅表里删除一个 事件
 * @param s 订阅表
 * @return 32位状态码
 */
eb_ret_t eb_sub_remove(const eb_sub_t* s) {
    if (!s) return EB_ERR_BADARG;
    if (eb_is_frozen()) return EB_ERR_BADSTATE;
    const int32_t idx = idx_of(s->event_id);
    /* 判断返回的索引是否合法 */
    if (idx < 0 || idx >= (int32_t)EB_MAX_EVENTS) return EB_ERR_BADARG;
    uint32_t pm;
    eb_port_enter_critical(&pm);

    eb_bucket_t* b = &g_tbl[(uint32_t)idx];
    for (uint16_t i = 0; i < b->n; i++) {
        if (sub_equal(&b->subs[i], s)) {
            /* 将最后一个的位置给移动到删除的位置 */
            b->subs[i] = b->subs[b->n - 1u];
            b->n--;
            eb_port_exit_critical(pm);
            return EB_OK;
        }
    }

    eb_port_exit_critical(pm);
    return EB_ERR_BADARG;
}
/**
 * @brief 找到订阅者的信息
 * @param event_id 事件id
 * @param out_list 存储找到的某个事件的订阅信息
 * @param max 限制订阅信息的数量
 * @return 实际返回的订阅信息数量
 */
uint32_t eb_sub_find(uint32_t event_id, eb_sub_t* out_list, uint32_t max) {
    if (!out_list || max == 0u) return 0u;
    const int32_t idx = idx_of(event_id);
    if (idx < 0 || idx >= (int32_t)EB_MAX_EVENTS) return 0u;
    const eb_bucket_t* b = &g_tbl[(uint32_t)idx];
    if (eb_is_frozen()) {
        /* Freeze 后订阅表只读：无需临界区，零中断延迟 */
        const uint32_t n = (b->n < (uint16_t)max) ? (uint32_t)b->n : max;
        for (uint32_t i = 0; i < n; i++) out_list[i] = b->subs[i];
        return n;
    }
    /* Freeze 前（初始化阶段）：短临界区保护并发安全 */
    uint32_t pm;
    eb_port_enter_critical(&pm);
    const uint32_t n = (b->n < (uint16_t)max) ? (uint32_t)b->n : max;
    for (uint32_t i = 0; i < n; i++) out_list[i] = b->subs[i];
    eb_port_exit_critical(pm);
    return n;
}

#else /* EB_CFG_STATIC_FREEZE == 0 */

/* ====================== Dynamic mode: COW 双表 ====================== */

/**
 * @brief 空操作
 */
static inline void cpu_relax(void) {
    __asm volatile("nop");
}
/* 订阅表 */
typedef struct {
    eb_sub_t subs[EB_MAX_EVENTS][EB_MAX_SUBS]; /* 事件桶 + 事件订阅者信息 */
    uint16_t counts[EB_MAX_EVENTS];            /* 每个桶的订阅者数量 */
} eb_table_t;
/* 注册表 帮助处理动态订阅的原子操作 */
typedef struct {
    eb_table_t tables[2];
    volatile uint32_t active_idx;  /* 0 or 1 */
    volatile uint32_t readers[2];  /* 每个表的读者数量 */
    volatile uint32_t writer_lock; /* 0=free, 1=locked */
} eb_registry_t;
static eb_registry_t g_reg;

#if EB_COW_USE_MUTEXES
static osal_mutex_t writer_mutex;
CORE_WEAK void writer_lock_acquire(void* handle) {
    OSAL_mutex_lock(*(osal_mutex_t*)handle, OSAL_WAIT_FOREVER);  // 拿不到就挂起（让出 CPU）
}
CORE_WEAK void writer_lock_release(void* handle) {
    OSAL_mutex_unlock(*(osal_mutex_t*)handle);
}
#else
static osal_mutex_t writer_mutex;
/**
 * @brief 获取锁
 */
void writer_lock_acquire(void* handle) {
    (void)handle; /* 自旋锁模式不使用 handle */
    /* 不建议在 ISR/高优先级跑动态订阅；这里允许自旋，调用方应只在 init/低优先级使用。 */
    while (__atomic_exchange_n((uint32_t*)&g_reg.writer_lock, 1u, __ATOMIC_ACQUIRE) != 0u) {
        cpu_relax();
    }
}
/**
 * @brief 释放锁
 */
void writer_lock_release(void* handle) {
    (void)handle;
    __atomic_store_n((uint32_t*)&g_reg.writer_lock, 0u, __ATOMIC_RELEASE);
}
#endif
/**
 * @brief 读取信息 读者 +1 返回订阅者信息表
 * @return 订阅者表
 */
static inline uint32_t reader_acquire(void) {
    /* 获取活动事件订阅表 */
    const uint32_t idx = __atomic_load_n((uint32_t*)&g_reg.active_idx, __ATOMIC_ACQUIRE);
    /* 获取原来的读者数量后 +1 写回 */
    (void)__atomic_fetch_add((uint32_t*)&g_reg.readers[idx], 1u, __ATOMIC_ACQ_REL);
    return idx;
}
/**
 * @brief 将指定表的读者 -1
 * @param idx 活动表表
 */
static inline void reader_release(uint32_t idx) {
    (void)__atomic_fetch_sub((uint32_t*)&g_reg.readers[idx], 1u, __ATOMIC_ACQ_REL);
}
/**
 * @brief
 * @param a 订阅信息 a
 * @param b 订阅信息 b
 * @return 是否相等
 */
static bool sub_equal(const eb_sub_t* a, const eb_sub_t* b) {
    return (a->event_id == b->event_id) && (a->delivery == b->delivery) && (a->cb == b->cb) &&
           (a->mailbox == b->mailbox) && (a->user == b->user) && (a->key_mask == b->key_mask) &&
           (a->key_value == b->key_value) && (a->expected_type_tag == b->expected_type_tag);
}
/**
 * @brief 初始化订阅注册表 的活动表索引 以及写者锁
 */
void eb_sub_init(void) {
    memset(&g_reg, 0, sizeof(g_reg));
    __atomic_store_n((uint32_t*)&g_reg.active_idx, 0u, __ATOMIC_RELEASE);
    __atomic_store_n((uint32_t*)&g_reg.writer_lock, 0u, __ATOMIC_RELEASE);
#if EB_COW_USE_MUTEXES
    OSAL_mutex_create(&writer_mutex, "cow_writer", 1, 1);
#endif
}
/**
 * @brief 在非活动表里对指定事件添加一个订阅
 * @param s 订阅信息
 * @return 32位状态码
 */
eb_ret_t eb_sub_add(const eb_sub_t* s) {
    if (!s) return EB_ERR_BADARG;
    if (eb_is_frozen()) return EB_ERR_BADSTATE;
    /* 获取 订阅事件所在的桶索引 */
    const int32_t eidx = eb_event_index(s->event_id);
    if (eidx < 0 || eidx >= (int32_t)EB_MAX_EVENTS) return EB_ERR_BADARG;
    writer_lock_acquire(&writer_mutex);
    /* 获取活动订阅表的 索引 */
    const uint32_t active = __atomic_load_n((uint32_t*)&g_reg.active_idx, __ATOMIC_ACQUIRE);
    const uint32_t shadow = active ^ 1u;
    /* 等待 shadow 表没有读者再覆盖 */
    while (__atomic_load_n((uint32_t*)&g_reg.readers[shadow], __ATOMIC_ACQUIRE) != 0u) {
        cpu_relax();
    }
    /* 将最新的读者信息给非活动列表 */
    memcpy(&g_reg.tables[shadow], &g_reg.tables[active], sizeof(g_reg.tables[0]));
    /* 返回每一个桶的 订阅者数量存储值所在的地址 */
    uint16_t* cnt = &g_reg.tables[shadow].counts[(uint32_t)eidx];
    if (*cnt >= (uint16_t)EB_MAX_SUBS) {
        writer_lock_release(&writer_mutex);
        // TODO 断言
        return EB_ERR_FULL;
    }

    /* 重复订阅直接 OK */
    for (uint16_t i = 0; i < *cnt; i++) {
        if (sub_equal(&g_reg.tables[shadow].subs[(uint32_t)eidx][i], s)) {
            __atomic_store_n((uint32_t*)&g_reg.active_idx, shadow, __ATOMIC_RELEASE);
            writer_lock_release(&writer_mutex);
            return EB_OK;
        }
    }
    /* 非活动订阅表指定桶 添加一个 订阅信息 */
    g_reg.tables[shadow].subs[(uint32_t)eidx][*cnt] = *s;
    (*cnt)++;
    /* 切换活动订阅表 */
    __atomic_store_n((uint32_t*)&g_reg.active_idx, shadow, __ATOMIC_RELEASE);
    /* 释放锁 */
    writer_lock_release(&writer_mutex);
    return EB_OK;
}
/**
 * @brief 将订阅信息从桶里面删除
 * @param s 订阅i信息
 * @return
 */
eb_ret_t eb_sub_remove(const eb_sub_t* s) {
    if (!s) return EB_ERR_BADARG;
    if (eb_is_frozen()) return EB_ERR_BADSTATE;
    /* 获取事件所在桶索引 */
    const int32_t eidx = eb_event_index(s->event_id);
    if (eidx < 0 || eidx >= (int32_t)EB_MAX_EVENTS) return EB_ERR_BADARG;

    writer_lock_acquire(&writer_mutex);

    const uint32_t active = __atomic_load_n((uint32_t*)&g_reg.active_idx, __ATOMIC_ACQUIRE);
    const uint32_t shadow = active ^ 1u;
    /* 等待没有读者再进行删除 */
    while (__atomic_load_n((uint32_t*)&g_reg.readers[shadow], __ATOMIC_ACQUIRE) != 0u) {
        cpu_relax();
    }
    /* 复制最新的活动列表信息 */
    memcpy(&g_reg.tables[shadow], &g_reg.tables[active], sizeof(g_reg.tables[0]));
    /* 获取事件订阅者数量 */
    uint16_t* cnt = &g_reg.tables[shadow].counts[(uint32_t)eidx];
    for (uint16_t i = 0; i < *cnt; i++) {
        /* 找到相等的订阅信息 */
        if (sub_equal(&g_reg.tables[shadow].subs[(uint32_t)eidx][i], s)) {
            /* 将最后一个存储到需要删除的位置 */
            g_reg.tables[shadow].subs[(uint32_t)eidx][i] =
                g_reg.tables[shadow].subs[(uint32_t)eidx][*cnt - 1u];
            (*cnt)--;
            /* 切换活动列表 */
            __atomic_store_n((uint32_t*)&g_reg.active_idx, shadow, __ATOMIC_RELEASE);
            writer_lock_release(&writer_mutex);
            return EB_OK;
        }
    }

    writer_lock_release(&writer_mutex);
    return EB_ERR_BADARG;
}
/**
 * @brief 找到存储找到的某个事件的订阅信息
 * @param event_id 事件id
 * @param out_list 存储找到的某个事件的订阅信息
 * @param max 限定订阅信息的返回数量
 * @return 实际找的数量
 */
uint32_t eb_sub_find(uint32_t event_id, eb_sub_t* out_list, uint32_t max) {
    if (!out_list || max == 0u) return 0u;

    const int32_t eidx = eb_event_index(event_id);
    if (eidx < 0 || eidx >= (int32_t)EB_MAX_EVENTS) return 0u;
    const uint32_t tidx = reader_acquire();

    const uint16_t cnt  = g_reg.tables[tidx].counts[(uint32_t)eidx];
    const uint32_t n    = ((uint32_t)cnt < max) ? (uint32_t)cnt : max;
    for (uint32_t i = 0; i < n; i++) {
        out_list[i] = g_reg.tables[tidx].subs[(uint32_t)eidx][i];
    }

    reader_release(tidx);
    return n;
}

#endif /* EB_CFG_STATIC_FREEZE */
