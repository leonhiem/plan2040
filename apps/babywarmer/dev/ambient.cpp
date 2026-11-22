/**
 * apps/babywarmer/dev/ambient.cpp — /dev/ambient: read-only, the room
 * NTC via the same ADS1115 (channels 2-3, differential)
 *
 * From picoos @ 89c16e5 (dev/ambient.cpp): the same as /dev/skintemp,
 * the other channel, the same NTC table (ntc.cpp); both come from the
 * one "adc" task (ntcadc.cpp). prog/safelut (safe mode) is what reads
 * it: the heater power for a given room temperature.
 *
 * Changed for plan2040: as /dev/skintemp (2026-11-21 bus and address
 * from board.h, errors instead of a hang; 2026-11-23 background sampling,
 * ntcadc.cpp, answered at once).
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
    float t_ambient = ntc_convert((uint16_t)code);
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
