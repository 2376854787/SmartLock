#include "fault_capture.h"

#include <stddef.h>

#include "assert_cus.h"
#include "blackbox_record.h"

void FaultRecord_Commit(bb_crash_type_t type, const fault_ctx_t *ctx) {
    ASSERT_PARAM(ctx != NULL);
    REQUIRE_RET_VOID(ctx != NULL);
    BB_RecordFault(type, ctx->pc, ctx->lr, ctx->sp, ctx->psr, ctx->cfsr, ctx->hfsr, ctx->dfsr,
                   ctx->mmfar, ctx->bfar, ctx->afsr);
}
