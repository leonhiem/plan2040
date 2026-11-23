/**
 * apps/babywarmer/dev/skintemp.cpp — /dev/skintemp: read-only, the skin
 * NTC via the ADS1115 (channels 0-1, differential)
 *
 * From picoos @ 89c16e5 (dev/skintemp.cpp). cat /dev/skintemp -> the
 * skin temperature, e.g. "36.5\n": the latest reading (at most ~100 ms
 * old) taken by the "adc" task in the background (ntcadc.h), answered at
 * once: no waiting for a conversion.
 *
 * Changed for plan2040: 2026-11-21 the bus and address from board.h, and
 * errors instead of a hang; 2026-11-23 our own ADS1115 driver and the
 * background sampling (ntcadc.cpp): picoos converted on every read and
 * waited ~8 ms for it. When there's no fresh reading, read() fails with
 * the reason: "ADS1115 (0x48, header bus) not answering".
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
