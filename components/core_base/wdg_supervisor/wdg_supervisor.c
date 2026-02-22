#include "APP_config.h"

#if (defined(CFG_FEAT_WDG_SUPERVISOR) && (CFG_FEAT_WDG_SUPERVISOR == 1))

#include <limits.h>
#include <string.h>

#include "barrier.h"
#include "hal_time.h"
#include "hal_wdg.h"
#include "osal.h"
#include "wdg_supervisor.h"

/* ===================== mailbox：SPSC 无锁挑战应答 ===================== */
/* 挑战题结构体 */
typedef struct {
    volatile uint32_t seq;   /* 序列号 */
    volatile uint32_t nonce; /* 随机数 */
    volatile uint32_t algo;  /* 算法类型 */
    volatile uint32_t param; /* 算法参数 */
} wdg_chal_t;
/* 回答 结构体 */
typedef struct {
    volatile uint32_t seq;    /* 序列号 */
    volatile uint32_t value;  /* 回复的答案 */
    volatile uint32_t status; /* 状态 */
} wdg_resp_t;
/* 邮箱 */
typedef struct {
    wdg_chal_t chal; /* 挑战题 */
    wdg_resp_t resp; /* 回复的答案 */
} wdg_mailbox_t;

/* ===================== 监督对象表 ===================== */
typedef struct {
    bool used;               /* 是否被使用 */
    wdg_watch_type_t type;   /* 监督类型 */
    wdg_algo_t algo;         /* 算法类型 */
    uint32_t key;            /* 任务私钥 */
    uint32_t param;          /* 迭代次数等 */
    uint32_t deadline_ms;    /* 挑战超时阈值（毫秒） */
    uint32_t hb_miss_budget; /* 允许缺失周期窗口，最小为1 */
    uint32_t hb_miss_count;  /* 当前连续缺失计数 */
    uint32_t chal_tick_ms;   /* 本轮挑战发布时间戳 */
    const char *name;        /* 名称 */
} wdg_watch_t;

static wdg_watch_t s_watch[WDG_SUP_MAX_WATCH];       // 记录有哪些任务被监控，以及它们的规则
static wdg_mailbox_t s_mb[WDG_SUP_MAX_WATCH];        // 每个任务一个专属邮箱
static uint32_t s_last_chal_seq[WDG_SUP_MAX_WATCH];  // 记录每个任务最后一次做的题号，防重复做题
static uint32_t s_boot_tick0            = 0;         // 开机时的系统Tick（毫秒）
static uint32_t s_period_ms             = 200;       // 老师巡查的周期，默认200ms
static uint32_t s_boot_grace_ms         = 6000;      // 开机宽限期 默认6s

static uint32_t s_required_hb_mask      = 0;  // 需要心跳的位掩码（哪些位被监控了）
static uint32_t s_seen_hb_mask          = 0;  // 实际收到心跳的位掩码

static uint32_t s_required_ch_mask      = 0;  // 需要做挑战题的位掩码 辅助快速从s_watch 查找

static volatile bool s_inited           = false;
static volatile bool s_fail_latched     = false;  // 死亡锁存器：一旦置为true说明系统没救了等死
static volatile wdg_sup_state_t s_state = WDG_SUP_STATE_INIT;  // 当前监督器运行状态 初始状态
static uint32_t s_seq                   = 0;                   // 全局题目期号，每次发题+1
static uint32_t s_prng                  = 0xC001D00Du;         // 伪随机数种子

#if (defined(CFG_FEAT_OSAL_BACKEND_CMSIS_OS2) && (CFG_FEAT_OSAL_BACKEND_CMSIS_OS2 == 1))
static osal_thread_t s_sup_thread = NULL;
#endif

/* ===================== 可控时延算法：Math Mix32 ===================== */
/**
 * @brief 使用时延可控算法生成老师需要的答案
 * @param nonce 随机数 老师生成
 * @param key 任务密钥
 * @param iters 迭代次数
 * @return 计算得到的挑战应答值
 */
static uint32_t wdg_math_mix32(uint32_t nonce, uint32_t key, uint32_t iters) {
    uint32_t x = nonce ^ key ^ 0xA5A5A5A5u;
    for (uint32_t i = 0; i < iters; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        x = x * 1664525u + 1013904223u;
        x ^= (x >> 16);
    }
    return x;
}
/**
 * @brief 老师使用正确的题目和密钥生成正确的答案 以便和任务提交的答案对比
 * @param id 任务id
 * @param nonce 随机数
 * @param seq 序列号
 * @return 期望答案值
 */
static uint32_t wdg_expected_calc(uint8_t id, uint32_t nonce, uint32_t seq) {
    (void)seq; /* 当前算法不需要 seq，但保留接口，后续可混入 seq */
    const wdg_watch_t *w = &s_watch[id];
    if (w->algo == WDG_ALGO_MATH_MIX32) {
        return wdg_math_mix32(nonce, w->key, w->param);
    }
    return 0xDEAD0001u;
}
/**
 * @brief 轻量级伪随机数生成器
 * @return 伪随机数
 */
static uint32_t prng_next(void) {
    uint32_t x = s_prng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_prng = x;
    return x;
}

typedef enum {
    RESP_PENDING      = 0, /* 尚未响应本题 */
    RESP_OK           = 1, /* 正确 */
    RESP_BAD_STATUS   = 2, /* 任务回包状态异常 */
    RESP_WRONG_ANSWER = 3, /* 回答错误 */
} wdg_resp_result_t;

/**
 * @brief 失败钩子默认弱实现，可在 port 层覆盖
 */
__attribute__((weak)) void wdg_sup_fail_hook(uint8_t id, uint32_t seq, uint32_t nonce,
                                             uint32_t expected, uint32_t got, uint32_t reason) {
    (void)id;
    (void)seq;
    (void)nonce;
    (void)expected;
    (void)got;
    (void)reason;
}
/**
 * @brief 当死亡锁存器被设置 为 True 死循环等待看门狗复位
 */
static void latch_fail_and_wait_reset(void) {
    s_fail_latched = true;
    s_state        = WDG_SUP_STATE_FAIL_LATCHED;
    /* 停止喂狗，等待 IWDG 复位；可进入安全态 */
    while (1) {
#if defined(__WFI)
        __WFI();
#endif
    }
}
/**
 * @brief 初始化看门狗全局变量
 * @param period_ms 监控周期
 * @param boot_grace_ms 开机宽限时间
 * @return RET_OK:成功，其他:错误码
 */
ret_code_t wdg_sup_init(uint32_t period_ms, uint32_t boot_grace_ms) {
    if (period_ms == 0u) return RET_MAKE_PARAM(RET_MOD_SYS, RET_SUB_SYS_WDG, RET_R_RANGE_ERR);
    /* 初始化监督表、任务邮箱 、每个任务最后做的题号*/
    memset(s_watch, 0, sizeof(s_watch));
    memset(s_mb, 0, sizeof(s_mb));
    memset(s_last_chal_seq, 0, sizeof(s_last_chal_seq));
    /* 设置检查周期 */
    s_period_ms        = period_ms;
    /* 设置开机宽限时间 */
    s_boot_grace_ms    = boot_grace_ms;
    /* 初始化需要被监控的心跳掩码 */
    s_required_hb_mask = 0;
    /* 初始化实际的心跳掩码 */
    s_seen_hb_mask     = 0;
    /* 需要挑战做题的掩码 */
    s_required_ch_mask = 0;
    /* 死亡锁存为 false */
    s_fail_latched     = false;
    s_state            = WDG_SUP_STATE_INIT;
    /* 全局题目号 */
    s_seq              = 0;
    /* 开机时的题目号 */
    s_boot_tick0       = hal_get_tick_ms();
    /* 随机数初始化为 系统tick */
    s_prng ^= s_boot_tick0;

#if (defined(CFG_FEAT_OSAL_BACKEND_CMSIS_OS2) && (CFG_FEAT_OSAL_BACKEND_CMSIS_OS2 == 1))
    /* 任务句柄 */
    s_sup_thread = NULL;
#endif
    s_inited = true;
    return RET_OK;
}
/**
 * @brief 任务注册登记
 * @param out_id 返回 id
 * @param name 名称
 * @param type 做题类型
 * @param algo 算法类型
 * @param key  密钥
 * @param param 参数 哈希迭代次数
 * @param deadline_ms 做题限时时间
 * @return 32位状态码
 */
ret_code_t wdg_sup_register(uint8_t *out_id, const char *name, wdg_watch_type_t type,
                            wdg_algo_t algo, uint32_t key, uint32_t param, uint32_t deadline_ms) {
    /* 参数检查 */
    if (!s_inited) return RET_MAKE_STATE(RET_MOD_SYS, RET_SUB_SYS_WDG, RET_R_NOT_READY);
    if (out_id == NULL) return RET_MAKE_PARAM(RET_MOD_SYS, RET_SUB_SYS_WDG, RET_R_NULL_PTR);
    /* 遍历找到一个没有被使用的位置用于存储当前 任务的注册 */
    for (uint32_t i = 0; i < WDG_SUP_MAX_WATCH; i++) {
        if (!s_watch[i].used) {
            s_watch[i].used           = true;
            s_watch[i].type           = type;
            s_watch[i].algo           = algo;
            s_watch[i].key            = key;
            s_watch[i].param          = (param == 0u) ? 1u : param;
            s_watch[i].deadline_ms    = deadline_ms;
            s_watch[i].hb_miss_budget = 1u;
            s_watch[i].hb_miss_count  = 0u;
            s_watch[i].chal_tick_ms   = 0u;
            s_watch[i].name           = (name != NULL) ? name : "unnamed";
            /* 心跳类型 */
            if (type == WDG_WATCH_HEARTBEAT) {
                s_required_hb_mask |= (1u << i);
            } else {
                /* 做题挑战 */
                if (deadline_ms == 0u)
                    return RET_MAKE_PARAM(RET_MOD_SYS, RET_SUB_SYS_WDG, RET_R_RANGE_ERR);
                s_required_ch_mask |= (1u << i);
            }
            /* 返回id */
            *out_id = (uint8_t)i;
            return RET_OK;
        }
    }
    return RET_MAKE_STATE(RET_MOD_SYS, RET_SUB_SYS_WDG, RET_R_TIMEOUT);
}
/**
 * @brief 任务心跳上报
 * @param id 任务id
 * @return 32位状态码
 */
ret_code_t wdg_sup_heartbeat(uint8_t id) {
    /* 确保已经初始化 */
    if (!s_inited) return RET_MAKE_STATE(RET_MOD_SYS, RET_SUB_SYS_WDG, RET_R_NOT_READY);
    /* 确保有效id */
    if (id >= WDG_SUP_MAX_WATCH)
        return RET_MAKE_PARAM(RET_MOD_SYS, RET_SUB_SYS_WDG, RET_R_RANGE_ERR);
    /* 确保该id 已经初始化 且监控类型为心跳*/
    if (!s_watch[id].used || s_watch[id].type != WDG_WATCH_HEARTBEAT)
        return RET_MAKE_PARAM(RET_MOD_SYS, RET_SUB_SYS_WDG, RET_R_RANGE_ERR);

#if (defined(CFG_FEAT_OSAL_BACKEND_CMSIS_OS2) && (CFG_FEAT_OSAL_BACKEND_CMSIS_OS2 == 1))
    /* 临界区变量 */
    osal_crit_state_t cs = 0;
    OSAL_enter_critical_ex(&cs);
    s_seen_hb_mask |= (1u << id);
    OSAL_exit_critical_ex(cs);
#else
    s_seen_hb_mask |= (1u << id);
#endif
    return RET_OK;
}

/**
 * @brief 任务的做题任务
 * @param id 任务id
 * @return 32位状态码
 */
ret_code_t wdg_sup_task_service(uint8_t id) {
    /* 确保已经初始化 */
    if (!s_inited) {
        return RET_MAKE_STATE(RET_MOD_SYS, RET_SUB_SYS_WDG, RET_R_NOT_READY);
    }
    /* 确保id 在有效范围内 */
    if (id >= WDG_SUP_MAX_WATCH) {
        return RET_MAKE_PARAM(RET_MOD_SYS, RET_SUB_SYS_WDG, RET_R_RANGE_ERR);
    }
    /* 确保该id已经注册 且类型为 挑战 */
    if (!s_watch[id].used || s_watch[id].type != WDG_WATCH_CHALLENGE) {
        return RET_MAKE_PARAM(RET_MOD_SYS, RET_SUB_SYS_WDG, RET_R_RANGE_ERR);
    }
    /* 获取邮箱地址 */
    wdg_mailbox_t *mb  = &s_mb[id];
    /* 获取挑战题号 */
    const uint32_t seq = mb->chal.seq;
    if (seq == 0u) {
        return RET_OK;
    }
    /* 读到 seq 后再读 nonce/algo/param */
    mem_barrier();
    /* 确保不做重复的题 */
    if (seq == s_last_chal_seq[id]) {
        return RET_OK;
    }
    const uint32_t nonce = mb->chal.nonce;
    /* 做题 */
    const uint32_t value = wdg_expected_calc(id, nonce, seq);
    mb->resp.value       = value;
    mb->resp.status      = 0u;
    /* 先写 value/status，再发布 resp.seq */
    mem_barrier();
    mb->resp.seq        = seq;
    /* 更新最后做的题 */
    s_last_chal_seq[id] = seq;
    return RET_OK;
}

/* ===================== Supervisor 单周期执行（poll 核心） ===================== */
/**
 * @brief 判断是否还在上电窗口期内
 * @return 是否还在上电窗口期内
 * @note 内部辅助函数
 */
static bool in_boot_grace_window(void) {
    const uint32_t now = hal_get_tick_ms();
    return (s_boot_grace_ms > 0u) && ((now - s_boot_tick0) < s_boot_grace_ms);
}

/**
 * @brief 更新监督器运行状态
 * @return true:本周期刚从 WARMUP 进入 RUN，false:未发生该切换
 * @note 内部辅助函数
 */
static bool update_runtime_state(void) {
    bool entered_run = false;
    if (s_state == WDG_SUP_STATE_INIT) {
        s_state = WDG_SUP_STATE_WARMUP;
    }
    if (s_state == WDG_SUP_STATE_WARMUP && !in_boot_grace_window()) {
        s_state     = WDG_SUP_STATE_RUN;
        entered_run = true;
    }
    return entered_run;
}

/**
 * @brief 重置所有 HEARTBEAT 任务的缺失窗口计数
 * @note 仅在 WARMUP 期间调用，避免启动阶段误判
 */
static void reset_heartbeat_windows(void) {
    for (uint32_t i = 0; i < WDG_SUP_MAX_WATCH; i++) {
        if (s_watch[i].used && s_watch[i].type == WDG_WATCH_HEARTBEAT) {
            s_watch[i].hb_miss_count = 0u;
        }
    }
}

/**
 * @brief 检查 HEARTBEAT 任务是否超出允许缺失窗口
 * @param seq 当前监督序列号，用于失败记录
 * @note 任一任务达到缺失上限即锁存失败，停止喂狗等待硬复位
 */
static void check_heartbeat_or_fail(uint32_t seq) {
    for (uint32_t i = 0; i < WDG_SUP_MAX_WATCH; i++) {
        /* 找出是 心跳监测的任务 id*/
        if (!(s_required_hb_mask & (1u << i))) continue;
        /* 查看当前id的心跳是否是 有响应 */
        const bool seen = ((s_seen_hb_mask & (1u << i)) != 0u);
        /* 缺失复位 */
        if (seen) {
            s_watch[i].hb_miss_count = 0u;
            continue;
        }
        /* 缺失计数 +1 */
        if (s_watch[i].hb_miss_count < UINT32_MAX) {
            s_watch[i].hb_miss_count++;
        }
        /* 缺失计数大于 指定配置 记录并打印错误 */
        if (s_watch[i].hb_miss_count >= s_watch[i].hb_miss_budget) {
            wdg_sup_fail_hook((uint8_t)i, seq, 0u, s_watch[i].hb_miss_budget,
                              s_watch[i].hb_miss_count, WDG_SUP_FAIL_HB_MISSING);
            latch_fail_and_wait_reset();
        }
    }
}
/**
 * @brief 给任务邮箱推送做题的资源
 * @param id 任务id
 * @param seq 序列号
 * @param nonce 随机数
 * @note 内部辅助函数
 */
static void publish_challenge(uint8_t id, uint32_t seq, uint32_t nonce) {
    /* 获取到邮箱 */
    wdg_mailbox_t *mb        = &s_mb[id];
    s_watch[id].chal_tick_ms = hal_get_tick_ms();
    /* 推送任务做题需要的东西 */
    mb->chal.nonce           = nonce;
    mb->chal.algo            = (uint32_t)s_watch[id].algo;
    mb->chal.param           = s_watch[id].param;

    /* 先写 nonce/algo/param，再发布 chal.seq */
    mem_barrier();
    mb->chal.seq = seq;
}
/**
 * @brief 检查一次 challenge 响应结果
 * @param id 任务id
 * @param seq 当前挑战序号
 * @param nonce 当前挑战随机数
 * @param out_got 输出：任务回包值（可为 NULL）
 * @param out_expected 输出：监督器期望值（可为 NULL）
 * @return 响应状态：pending/ok/bad_status/wrong_answer
 */
static wdg_resp_result_t check_one_response(uint8_t id, uint32_t seq, uint32_t nonce,
                                            uint32_t *out_got, uint32_t *out_expected) {
    /* 获取邮箱 */
    const wdg_mailbox_t *mb = &s_mb[id];
    /* 获取任务计算的题目号 */
    const uint32_t rseq     = mb->resp.seq;
    /* 题目不符 */
    if (rseq != seq) return RESP_PENDING;

    /* 读到 resp.seq 后再读 value/status */
    mem_barrier();
    /* 获取任务计算的答案 */
    const uint32_t got      = mb->resp.value;
    /* 计算正确值 */
    const uint32_t expected = wdg_expected_calc(id, nonce, seq);
    /* 将任务计算的答案返回出去 */
    if (out_got) *out_got = got;
    /* 将题目的正确答案 返回 */
    if (out_expected) *out_expected = expected;

    if (mb->resp.status != 0u) return RESP_BAD_STATUS;
    /* 判断答案是否正确 */
    if (got != expected) return RESP_WRONG_ANSWER;
    return RESP_OK;
}

/**
 * @brief 执行一轮 challenge 出题与统一判题
 * @param seq 本轮挑战序号
 * @param nonce 本轮挑战随机数
 * @note 本轮所有 CHALLENGE 任务均答对后才返回；任一失败立即锁存
 */
static void run_challenge_round(uint32_t seq, uint32_t nonce) {
    /* 给注册挑战的任务 发送题目 */
    for (uint32_t i = 0; i < WDG_SUP_MAX_WATCH; i++) {
        if (s_watch[i].used && s_watch[i].type == WDG_WATCH_CHALLENGE) {
            publish_challenge((uint8_t)i, seq, nonce);
        }
    }
    while (1) {
        /* 判断是否锁存死亡 */
        if (s_fail_latched) latch_fail_and_wait_reset();

        bool all_ok = true;
        for (uint32_t i = 0; i < WDG_SUP_MAX_WATCH; i++) {
            /* 剔除掉不是挑战的 id */
            if (!(s_required_ch_mask & (1u << i))) continue;

            uint32_t got      = UINT32_MAX;
            uint32_t expected = 0u;
            /* 进行题目答案比对 */
            const wdg_resp_result_t rs =
                check_one_response((uint8_t)i, seq, nonce, &got, &expected);
            /* 通过挑战 通过进行下一个id */
            if (rs == RESP_OK) {
                continue;
            }
            /* 未通过挑战 */
            all_ok = false;
            /* 有响应但错误：立即失败，不再继续等待 */
            if (rs == RESP_BAD_STATUS || rs == RESP_WRONG_ANSWER) {
                wdg_sup_fail_hook((uint8_t)i, seq, nonce, expected, got, WDG_SUP_FAIL_WRONG_ANSWER);
                latch_fail_and_wait_reset();
            }

            /* 尚未响应：按“本任务发布时间戳”独立判超时 */
            const uint32_t now     = hal_get_tick_ms();
            const uint32_t elapsed = now - s_watch[i].chal_tick_ms;
            /* 判断是否是超时 */
            if (elapsed > s_watch[i].deadline_ms) {
                /* 挑战失败且超时 */
                expected = wdg_expected_calc((uint8_t)i, nonce, seq);
                wdg_sup_fail_hook((uint8_t)i, seq, nonce, expected, got, WDG_SUP_FAIL_TIMEOUT);
                latch_fail_and_wait_reset();
            }
        }

        if (all_ok) break;
#if defined(CFG_FEAT_OSAL_BACKEND_CMSIS_OS2) && (CFG_FEAT_OSAL_BACKEND_CMSIS_OS2 == 1)
        if (OSAL_kernel_is_running()) {
            OSAL_delay_ms(1);
        }
#elif defined(__WFI)
        __WFI();
#endif
    }
}

/**
 * @brief Supervisor 单周期执行
 */
static ret_code_t supervisor_one_cycle(void) {
    /* 确保已经初始化 且 状态正常 */
    if (s_fail_latched || s_state == WDG_SUP_STATE_FAIL_LATCHED) {
        latch_fail_and_wait_reset();
    }
    const bool just_entered_run = update_runtime_state();
    /*　判断是否是在run状态 且不是刚进入的 */
    if (s_state == WDG_SUP_STATE_RUN && !just_entered_run) {
        /* 检查 HEARTBEAT 任务是否超出允许缺失窗口 */
        check_heartbeat_or_fail(s_seq);
    } else {
        /* WARMUP：仅保活，不做严格心跳判死 */
        reset_heartbeat_windows();
    }
    /* 复位心跳掩码 */
    s_seen_hb_mask = 0u;
    uint32_t seq   = s_seq;
    uint32_t nonce = 0u;
    /*　判断是否是在run状态 且不是刚进入的 */
    if (s_state == WDG_SUP_STATE_RUN && !just_entered_run && s_required_ch_mask != 0u) {
        s_seq++;
        seq   = s_seq;
        nonce = prng_next() ^ hal_get_tick_ms() ^ (seq * 0x9E3779B9u);
        /* 执行一轮 challenge 出题与统一判题 */
        run_challenge_round(seq, nonce);
    }
    /* 喂狗 */
    const ret_code_t rc = hal_wdg_kick();
    if (ret_is_err(rc)) {
        wdg_sup_fail_hook(WDG_SUP_ID_WDG_KICK, seq, nonce, 0u, (uint32_t)rc, WDG_SUP_FAIL_FRESH);
        latch_fail_and_wait_reset();
    }

    return RET_OK;
}

/* ===================== 运行模式：RTOS / 裸机 ===================== */

#if (defined(CFG_FEAT_OSAL_BACKEND_CMSIS_OS2) && (CFG_FEAT_OSAL_BACKEND_CMSIS_OS2 == 1))
/**
 * @brief RTOS任务用于看门狗
 * @param arg 参数
 */
static void wdg_sup_thread(void *arg) {
    (void)arg;
    uint32_t last = hal_get_tick_ms();
    while (1) {
        const uint32_t now = hal_get_tick_ms();
        if ((now - last) >= s_period_ms) {
            last = now;
            (void)supervisor_one_cycle();
        }
        (void)OSAL_delay_ms(1);
    }
}
#endif
/**
 * @brief 创建RTOS 任务用于看门狗的出题、检查
 * @return 32 位状态码
 */
ret_code_t wdg_sup_start(void) {
    /* 初始化检查 */
    if (!s_inited) return RET_MAKE_STATE(RET_MOD_SYS, RET_SUB_SYS_WDG, RET_R_NOT_READY);
    /* OSAL环境检查 */
#if (defined(CFG_FEAT_OSAL_BACKEND_CMSIS_OS2) && (CFG_FEAT_OSAL_BACKEND_CMSIS_OS2 == 1))
    if (s_sup_thread != NULL) return RET_OK;

    const osal_thread_attr_t attr = {
        .name       = "wdg_sup",
        .stack_size = 512u,
        .priority   = OSAL_PRIO_HIGH,
    };
    const ret_code_t rc = OSAL_thread_create(&s_sup_thread, wdg_sup_thread, NULL, &attr);
    if (ret_is_err(rc)) return rc;
    return RET_OK;
#else
    /* 裸机模式无需 start */
    return RET_OK;
#endif
}
/**
 * @brief 用于裸机看门狗周期任务
 * @return 32位状态码
 */
ret_code_t wdg_sup_poll(void) {
    /* 初始化检查 */
    if (!s_inited) return RET_MAKE_STATE(RET_MOD_SYS, RET_SUB_SYS_WDG, RET_R_NOT_READY);
    static uint32_t last = 0;
    const uint32_t now   = hal_get_tick_ms();
    if (last == 0) last = now;

    if ((now - last) >= s_period_ms) {
        last = now;
        return supervisor_one_cycle();
    }
    return RET_OK;
}

/**
 * @brief 设置 HEARTBEAT 任务允许缺失的周期窗口
 * @param id HEARTBEAT 任务id
 * @param miss_budget_cycles 允许连续缺失周期数（>=1）
 * @return RET_OK:成功，其他:错误码
 */
ret_code_t wdg_sup_set_hb_miss_budget(uint8_t id, uint32_t miss_budget_cycles) {
    /* 确保已经初始化 */
    if (!s_inited) return RET_MAKE_STATE(RET_MOD_SYS, RET_SUB_SYS_WDG, RET_R_NOT_READY);
    /* 确保id 有效范围内 */
    if (id >= WDG_SUP_MAX_WATCH)
        return RET_MAKE_PARAM(RET_MOD_SYS, RET_SUB_SYS_WDG, RET_R_RANGE_ERR);
    /* 确保id 类型为心跳 */
    if (!s_watch[id].used || s_watch[id].type != WDG_WATCH_HEARTBEAT)
        return RET_MAKE_PARAM(RET_MOD_SYS, RET_SUB_SYS_WDG, RET_R_RANGE_ERR);
    /* 检查合法的缺失周期窗口 */
    if (miss_budget_cycles == 0u)
        return RET_MAKE_PARAM(RET_MOD_SYS, RET_SUB_SYS_WDG, RET_R_RANGE_ERR);
    /* 设置缺失周期数 */
    s_watch[id].hb_miss_budget = miss_budget_cycles;
    return RET_OK;
}

/**
 * @brief 获取 HEARTBEAT 任务允许缺失的周期窗口
 * @param id HEARTBEAT 任务id
 * @return 允许缺失周期数；参数非法时返回0
 */
uint32_t wdg_sup_get_hb_miss_budget(uint8_t id) {
    if (id >= WDG_SUP_MAX_WATCH) return 0u;
    if (!s_watch[id].used || s_watch[id].type != WDG_WATCH_HEARTBEAT) return 0u;
    return s_watch[id].hb_miss_budget;
}

/**
 * @brief 计算 deadline 预算值（毫秒）
 * @param service_ms 任务服务周期（通常取 2 倍任务周期）
 * @param jitter_ms 调度抖动预算
 * @param compute_ms 算法计算耗时预算
 * @param margin_ms 安全余量
 * @return 预算结果；溢出时饱和到 UINT32_MAX
 */
uint32_t wdg_sup_deadline_budget_ms(uint32_t service_ms, uint32_t jitter_ms, uint32_t compute_ms,
                                    uint32_t margin_ms) {
    const uint64_t sum =
        (uint64_t)service_ms + (uint64_t)jitter_ms + (uint64_t)compute_ms + (uint64_t)margin_ms;
    if (sum > UINT32_MAX) return UINT32_MAX;
    return (uint32_t)sum;
}

/**
 * @brief 调试辅助：按 id 获取监控对象名称
 */
const char *wdg_sup_get_name(uint8_t id) {
    if (id == WDG_SUP_ID_SYSTEM_HEARTBEAT) return "system_heartbeat";
    if (id == WDG_SUP_ID_WDG_KICK) return "wdg_kick";
    if (id < WDG_SUP_MAX_WATCH && s_watch[id].used && s_watch[id].name != NULL) {
        return s_watch[id].name;
    }
    return "unknown";
}

/**
 * @brief 将失败原因码转换为可读字符串
 * @param reason 失败原因码
 * @return 原因字符串
 */
const char *wdg_sup_reason_str(uint32_t reason) {
    switch (reason) {
        case WDG_SUP_FAIL_TIMEOUT:
            return "timeout";
        case WDG_SUP_FAIL_WRONG_ANSWER:
            return "wrong_answer";
        case WDG_SUP_FAIL_HB_MISSING:
            return "hb_missing";
        case WDG_SUP_FAIL_FRESH:
            return "kick_failed";
        default:
            return "unknown";
    }
}

/**
 * @brief 获取监督器当前状态值
 * @return 监督器状态枚举
 */
wdg_sup_state_t wdg_sup_get_state(void) {
    return s_state;
}

/**
 * @brief 获取监督器当前状态名称
 * @return 状态字符串
 */
const char *wdg_sup_get_state_name(void) {
    switch (s_state) {
        case WDG_SUP_STATE_INIT:
            return "init";
        case WDG_SUP_STATE_WARMUP:
            return "warmup";
        case WDG_SUP_STATE_RUN:
            return "run";
        case WDG_SUP_STATE_FAIL_LATCHED:
            return "fail_latched";
        default:
            return "unknown";
    }
}

#endif
