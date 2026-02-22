#include "blackbox_record.h"
#include "osal.h"

/* 将 OSAL 临界区观测结果接入 blackbox */
void OSAL_on_critical_duration_us(uint32_t dur_us) {
    BB_UpdateMaxCriUs(dur_us);
}
