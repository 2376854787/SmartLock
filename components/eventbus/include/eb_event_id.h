#ifndef SMARTLOCK_EB_EVENT_ID_H
#define SMARTLOCK_EB_EVENT_ID_H
#include "eb_module_id.h"
/* SYS */
#define EB_EVT_SYS_BOOT      EB_EID(EB_MOD_SYS, 0x0001)  // Control, L, Edge
#define EB_EVT_SYS_TICK_1MS  EB_EID(EB_MOD_SYS, 0x0002)  // Data or Control? 先在表里定
#define EB_EVT_SYS_HARDFAULT EB_EID(EB_MOD_SYS, 0x0003)  // Control, H, Edge

/* BH1750 */
#define EB_EVT_BH1750_READY EB_EID(EB_MOD_BH1750, 0x0001)  // Control, M/L, Snapshot
#define EB_EVT_BH1750_ERROR EB_EID(EB_MOD_BH1750, 0x0002)  // Control, H/M, Edge

/* OLED */
#define EB_EVT_OLED_REFRESH_REQ EB_EID(EB_MOD_OLED, 0x0001)  // Control, L, Snapshot
#define EB_EVT_OLED_ERROR       EB_EID(EB_MOD_OLED, 0x0002)  // Control, M, Edge
#endif                                                       // SMARTLOCK_EB_EVENT_ID_H
