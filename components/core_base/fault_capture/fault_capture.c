#include "fault_capture.h"

#include "blackbox_record.h"

void FaultRecord_Commit(bb_crash_type_t type, const fault_ctx_t *ctx) {
    if (!ctx) return;
    BB_RecordFault(type, ctx->pc, ctx->lr, ctx->sp, ctx->psr, ctx->cfsr, ctx->hfsr, ctx->dfsr,
                   ctx->mmfar, ctx->bfar, ctx->afsr);
}
