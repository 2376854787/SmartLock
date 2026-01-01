#ifndef __GT9147_COMPAT_H
#define __GT9147_COMPAT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Compatibility layer for existing lvgl_port.c */
bool gt9147_init(void);
bool gt9147_read_point(uint16_t *x, uint16_t *y, bool *pressed);

#ifdef __cplusplus
}
#endif

#endif

