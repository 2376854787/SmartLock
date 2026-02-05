#ifndef SMARTLOCK_RAMFUNC_H
#define SMARTLOCK_RAMFUNC_H
#include "complier_cus.h"

#define CORE_RAMFUNC    CORE_SECTION(".ramfunc") CORE_NOINLINE
#define CORE_ITCM_TEXT  CORE_SECTION(".itcm_text") CORE_NOINLINE
#define CORE__DTCM_DATA CORE_SECTION(".dtcm_data")

void ramfunc_init_copy(void);
#endif  // SMARTLOCK_RAMFUNC_H
