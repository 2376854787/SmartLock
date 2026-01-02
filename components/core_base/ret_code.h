//
// 创建：yan，2025/12/20
//

#ifndef SMARTLOCK_RET_CODE_H
#define SMARTLOCK_RET_CODE_H
#include <stdint.h>
#include "cmsis_gcc.h"
#ifdef __cplusplus
extern "C" {



#endif

typedef enum {
    RET_OK = 0,

    /* generic errors */
    RET_E_FAIL = -1,
    RET_E_INVALID_ARG = -2,
    RET_E_TIMEOUT = -3,
    RET_E_BUSY = -4,
    RET_E_NO_MEM = -5,
    RET_E_NOT_FOUND = -6,
    RET_E_NOT_READY = -7,
    RET_E_DATA_OVERFLOW = -8,
    RET_E_UNSUPPORTED = -9,
    RET_E_IO = -10,
    RET_E_CRC = -11,
    RET_E_DATA_NOT_ENOUGH =-12,
    RET_E_ISR = -13,

    /* extend as needed... */
} ret_code_t;

static __INLINE int ret_is_ok(ret_code_t r) { return (r == RET_OK); }
static __INLINE int ret_is_err(ret_code_t r) { return (r < RET_OK); }

#ifdef __cplusplus
}
#endif

#endif //SMARTLOCK_RET_CODE_H
