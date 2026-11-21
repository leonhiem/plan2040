/**
 * apps/babywarmer/dev/skintemp.cpp — /dev/skintemp: read-only, the skin
 * NTC via the ADS1115 (channels 0-1, differential)
 *
 * From picoos @ 89c16e5 (dev/skintemp.cpp). cat /dev/skintemp -> a fresh
 * reading each time, e.g. "36.5\n". No polling task, no shared global:
 * the device owns its hardware state and reads synchronously whenever
 * it's read (one conversion at 128 SPS, ~8 ms plus the I2C).
 *
 * Changed for plan2040 (2026-11-21): the bus and address come from
 * board.h (ADS1115_I2C, the header bus on babywarmer_v1), and a chip
 * that doesn't answer makes open()/read() fail (cat reports an error)
 * instead of hanging the board (drivers/ads1115/README.md). open() tries
 * the chip again every time until it has answered once.
 */
#include "board.h"
#include "kernel/fs.h"
#include "ntc.h"
#include "drivers/ads1115/ads1115.h"
#include <cstdio>

static struct ads1115_adc adc;
static bool ready = false;

static int skintemp_open(void)
{
    if (!ready) {
        if (!ads1115_init(ADS1115_I2C, ADS1115_I2C_ADDR, &adc))
            return -1;
        ads1115_set_pga(ADS1115_PGA_1_024, &adc); // +/- 1V
        ads1115_set_data_rate(ADS1115_RATE_128_SPS, &adc);
        ready = true;
    }
    return 0;
}

static int skintemp_read(char *buf, int len)
{
    ads1115_set_input_mux(ADS1115_MUX_DIFF_0_1, &adc);
    uint16_t adc_value;
    if (!ads1115_write_config(&adc) || !ads1115_read_adc(&adc_value, &adc))
        return -1;

    float t_skin = ntc_convert(adc_value);
    return snprintf(buf, len, "%.1f\n", t_skin);
}

static const device_t dev_skintemp = {
    "/dev/skintemp",
    skintemp_open,
    0,               // no close needed
    skintemp_read,
    0,               // read-only
};

void dev_skintemp_register(void)
{
    fs_register(&dev_skintemp);
}
