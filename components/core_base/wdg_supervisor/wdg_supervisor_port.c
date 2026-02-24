#include "APP_config.h"

#if (defined(CFG_FEAT_WDG_SUPERVISOR) && (CFG_FEAT_WDG_SUPERVISOR == 1))

#include <stdio.h>

#include "assert_cus.h"
#include "blackbox_record.h"
#include "hal_time.h"
#include "wdg_supervisor.h"

#ifndef CFG_PARAM_WDG_PRE_RESET_PRINT_ENABLE
#define CFG_PARAM_WDG_PRE_RESET_PRINT_ENABLE 1
#endif

/**
 * @brief 看门狗失败钩子（port 层强实现）
 * @param id 失败任务id（含保留id）
 * @param seq 失败轮次序号
 * @param nonce 失败轮次随机数
 * @param expected 期望值（超时场景也会计算并记录）
 * @param got 实际值（未响应时通常为初始化值）
 * @param reason 失败原因码
 * @return 无
 * @note  先写 blackbox 持久化，再做可选同步打印。
 */
void wdg_sup_fail_hook(uint8_t id, uint32_t seq, uint32_t nonce, uint32_t expected, uint32_t got,
                       uint32_t reason) {
    const char *name       = wdg_sup_get_name(id);
    const char *reason_txt = wdg_sup_reason_str(reason);
    const char *state_txt  = wdg_sup_get_state_name();
    ASSERT_PARAM((name != NULL) && (reason_txt != NULL) && (state_txt != NULL));
    if (name == NULL) name = "unknown";
    if (reason_txt == NULL) reason_txt = "unknown";
    if (state_txt == NULL) state_txt = "unknown";
    const bb_wdg_fail_record_t rec = {
        .valid    = 1u,
        .task_id  = (uint32_t)id,
        .reason   = reason,
        .seq      = seq,
        .nonce    = nonce,
        .expected = expected,
        .got      = got,
        .tick_ms  = hal_get_tick_ms(),
    };

    BB_EnableAccess();
    BB_RecordWdgFail(&rec);

#if (CFG_PARAM_WDG_PRE_RESET_PRINT_ENABLE == 1)
    printf(
        "WDG失败: id=%lu name=%s reason=%lu(%s) state=%s seq=0x%08lX nonce=0x%08lX exp=0x%08lX "
        "got=0x%08lX tick=%lu\r\n",
        (unsigned long)rec.task_id, name, (unsigned long)rec.reason, reason_txt, state_txt,
        (unsigned long)rec.seq, (unsigned long)rec.nonce, (unsigned long)rec.expected,
        (unsigned long)rec.got, (unsigned long)rec.tick_ms);
#endif
}

#endif /* CFG_FEAT_WDG_SUPERVISOR */
