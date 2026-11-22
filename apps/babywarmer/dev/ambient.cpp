/**
 * apps/babywarmer/dev/ambient.cpp — /dev/ambient: read-only, the room
 * NTC via the same ADS1115 (channels 2-3, differential)
 *
 * From picoos @ 89c16e5 (dev/ambient.cpp): the same as /dev/skintemp,
 * the other channel, the same NTC table (ntc.cpp). Its own ads1115_adc
 * copy and open(): two devices sharing one chip is fine under the
 * cooperative scheduler (no read interrupts another mid-transfer).
 * prog/safelut (safe mode) is what reads it: the heater power for a
 * given room temperature.
 *
 * Changed for plan2040 (2026-11-21): as /dev/skintemp (bus and address
 * from board.h, errors instead of a hang).
 */
#include "board.h"
#include "kernel/fs.h"
#include "ntc.h"
#include "drivers/ads1115/ads1115.h"
#include "kernel/errstr.h"
#include <cstdio>

static struct ads1115_adc adc;
static bool ready = false;

static int ambient_open(void)
{
    if (!ready) {
        if (!ads1115_init(ADS1115_I2C, ADS1115_I2C_ADDR, &adc)) {
            werrstr("ADS1115 (0x%02x) not answering", ADS1115_I2C_ADDR);
            return -1;
        }
        ads1115_set_pga(ADS1115_PGA_1_024, &adc); // +/- 1V
        ads1115_set_data_rate(ADS1115_RATE_128_SPS, &adc);
        ready = true;
    }
    return 0;
}

static int ambient_read(char *buf, int len)
{
    ads1115_set_input_mux(ADS1115_MUX_DIFF_2_3, &adc);
    uint16_t adc_value;
    if (!ads1115_write_config(&adc) || !ads1115_read_adc(&adc_value, &adc)) {
        werrstr("ADS1115 (0x%02x) not answering", ADS1115_I2C_ADDR);
        return -1;
    }

    float t_ambient = ntc_convert(adc_value);
    return snprintf(buf, len, "%.1f\n", t_ambient);
}

static const device_t dev_ambient = {
    "/dev/ambient",
    ambient_open,
    0,               // no close needed
    ambient_read,
    0,               // read-only
};

void dev_ambient_register(void)
{
    fs_register(&dev_ambient);
}
