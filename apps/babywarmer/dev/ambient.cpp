/**
 * apps/babywarmer/dev/ambient.cpp — /dev/ambient: the room temperature
 *
 * As /dev/skintemp, on the ADS1115's channels 2-3, through the same NTC
 * table. Read by fftable (the feed-forward) and the guard (the safe
 * mode's power).
 */
#include "board.h"
#include "kernel/fs.h"
#include "ntc.h"
#include "ntcadc.h"
#include <cstdio>

static int ambient_read(char *buf, int len)
{
    int16_t code;
    if (!ntcadc_get(NTC_ROOM, &code)) return -1;     // errstr says why
    float t_ambient;
    if (!ntc_temp(code, &t_ambient)) return -1;     // outside the calibrated range: errstr says so
    return snprintf(buf, len, "%.1f\n", t_ambient);
}

static const device_t dev_ambient = {
    "/dev/ambient",
    0,               // no open needed
    0,               // no close needed
    ambient_read,
    0,               // read-only
};

void dev_ambient_register(void)
{
    ntcadc_init();
    fs_register(&dev_ambient);
}
