#ifndef SMARTLOCK_EB_MODULE_ID_H
#define SMARTLOCK_EB_MODULE_ID_H

typedef enum {
    EB_MOD_SYS     = 0x0001,
    EB_MOD_APP     = 0x0002,
    EB_MOD_SENSOR  = 0x0003,
    EB_MOD_BH1750  = 0x0004,
    EB_MOD_OLED    = 0x0005,
    EB_MOD_MOTOR   = 0x0006,
    EB_MOD_NET     = 0x0007,
    EB_MOD_STORAGE = 0x0008,
    EB_MOD_SPI     = 0x0009,
} eb_module_id_t;

#define EB_EID(mod, local) (((uint32_t)(mod) << 16) | ((uint32_t)(local) & 0xFFFFu))


#endif
