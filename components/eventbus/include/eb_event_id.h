#ifndef SMARTLOCK_EB_EVENT_ID_H
#define SMARTLOCK_EB_EVENT_ID_H
#include "eb_module_id.h"
/* SYS */
#define EB_EVT_SYS_BOOT EB_EID(EB_MOD_SYS, 0x0001)  // Control, L, Edge
#define EB_EVT_SYS_TICK_1MS \
    EB_EID(EB_MOD_SYS, 0x0002)  // Data Plane：1kHz 高频，禁止走 EventBus，走 SPSC/RB 直连
#define EB_EVT_SYS_HARDFAULT EB_EID(EB_MOD_SYS, 0x0003)  // Control, H, Edge

/* BH1750 */
#define EB_EVT_BH1750_READY EB_EID(EB_MOD_BH1750, 0x0001)  // Control, M/L, Snapshot
#define EB_EVT_BH1750_ERROR EB_EID(EB_MOD_BH1750, 0x0002)  // Control, H/M, Edge

/* OLED */
#define EB_EVT_OLED_REFRESH_REQ EB_EID(EB_MOD_OLED, 0x0001)  // Control, L, Snapshot
#define EB_EVT_OLED_ERROR       EB_EID(EB_MOD_OLED, 0x0002)  // Control, M, Edge

/* SPI */
#define EB_EVT_SPI_DONE        EB_EID(EB_MOD_SPI, 0x0001)  // Control, M, Edge
#define EB_EVT_SPI_ERROR       EB_EID(EB_MOD_SPI, 0x0002)  // Control, H, Edge
#define EB_EVT_SPI_STREAM_HALF EB_EID(EB_MOD_SPI, 0x0003)  // Control, M, Snapshot
#define EB_EVT_SPI_STREAM_FULL EB_EID(EB_MOD_SPI, 0x0004)  // Control, M, Snapshot

/* I2C */
#define EB_EVT_I2C_DONE  EB_EID(EB_MOD_I2C, 0x0001)  // Control, M, Edge
#define EB_EVT_I2C_ERROR EB_EID(EB_MOD_I2C, 0x0002)  // Control, H, Edge

/* RC522 */
#define EB_EVT_RC522_READY     EB_EID(EB_MOD_RC522, 0x0001)  // Control, M, Snapshot
#define EB_EVT_RC522_CARD      EB_EID(EB_MOD_RC522, 0x0002)  // Control, M, Snapshot
#define EB_EVT_RC522_CARD_LOST EB_EID(EB_MOD_RC522, 0x0003)  // Control, M, Edge
#define EB_EVT_RC522_ERROR     EB_EID(EB_MOD_RC522, 0x0004)  // Control, H, Edge
#endif                                                       // SMARTLOCK_EB_EVENT_ID_H
