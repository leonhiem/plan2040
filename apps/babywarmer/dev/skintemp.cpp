/**
 * apps/babywarmer/dev/skintemp.cpp — /dev/skintemp: the skin temperature
 *
 *   cat /dev/skintemp         e.g. "36.5"
 *
 * The ADS1115's channels 0-1 (differential) through the NTC table
 * (dev/ntc.cpp). The "adc" task samples in the background (ntcadc.h), so a
 * read answers at once with the latest reading (at most ~100 ms old). With
 * no fresh reading it fails with the reason: "ADS1115 (0x48, header bus)
 * not answering", or "code ... is outside the calibrated range (open or
 * shorted?)".
 */
#include "board.h"
#include "kernel/fs.h"
#include "ntc.h"
#include "ntcadc.h"
#include <cstdio>

static int skintemp_read(char *buf, int len)
{
    int16_t code;
    if (!ntcadc_get(NTC_SKIN, &code)) return -1;     // errstr says why
    float t_skin;
    if (!ntc_temp(code, &t_skin)) return -1;     // outside the calibrated range: errstr says so
    return snprintf(buf, len, "%.1f\n", t_skin);
}

static const device_t dev_skintemp = {
    "/dev/skintemp",
    0,               // no open needed
    0,               // no close needed
    skintemp_read,
    0,               // read-only
};

void dev_skintemp_register(void)
{
    ntcadc_init();
    fs_register(&dev_skintemp);
}
