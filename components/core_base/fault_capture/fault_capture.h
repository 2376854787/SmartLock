#ifndef FAULT_RECORD_H
#define FAULT_RECORD_H

#include <stdint.h>

#include "blackbox_record.h"

typedef struct {
    uint32_t pc, lr, sp, psr;
    uint32_t cfsr, hfsr, dfsr, mmfar, bfar, afsr;
} fault_ctx_t;

/* 平台无关：把 ctx 写入 blackbox */
void FaultRecord_Commit(bb_crash_type_t type, const fault_ctx_t *ctx);

#endif
