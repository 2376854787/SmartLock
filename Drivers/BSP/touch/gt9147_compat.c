#include "gt9147_compat.h"

#include "touch.h"

bool gt9147_init(void)
{
    return tp_init() == 0;
}

bool gt9147_read_point(uint16_t *x, uint16_t *y, bool *pressed)
{
    if (x == NULL || y == NULL || pressed == NULL)
    {
        return false;
    }

    if (tp_dev.scan == NULL)
    {
        return false;
    }

    if (tp_dev.scan(0) != 0)
    {
        return false;
    }

    *pressed = (tp_dev.sta & TP_PRES_DOWN) ? true : false;
    *x = tp_dev.x[0];
    *y = tp_dev.y[0];
    return true;
}

