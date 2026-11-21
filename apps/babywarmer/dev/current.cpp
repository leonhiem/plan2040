/**
 * apps/babywarmer/dev/current.cpp — /dev/current: read-only, the heater
 * current sense (RP2040 ADC0, GPIO26)
 *
 * From picoos @ 89c16e5 (dev/current.cpp). cat /dev/current -> raw ADC
 * counts, 0..4095, the average of 8 samples taken in the one read (no
 * conversion to amps is documented for this circuit, so raw counts it
 * is). prog/alarmcheck compares it with the heater's state.
 *
 * Changed for plan2040 (2026-11-21): the pin comes from board.h, and the
 * device sets up its own pin (picoos did it in setup_gpios()). The ADC
 * block is started only if nothing has yet: adc_init() resets it, which
 * would switch off the temperature sensor /dev/cputemp turned on.
 */
#include "board.h"
#include "kernel/fs.h"
#include "hardware/adc.h"
#include <cstdio>

#define CURRENT_ADC_INPUT (PIN_CURRENT_ADC - 26) // GPIO26..29 = ADC0..3

static int current_read(char *buf, int len)
{
    adc_select_input(CURRENT_ADC_INPUT);
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) sum += adc_read();
    return snprintf(buf, len, "%lu\n", (unsigned long)(sum / 8));
}

static const device_t dev_current = {
    "/dev/current",
    0,
    0,
    current_read,
    0,             // read-only
};

void dev_current_register(void)
{
    if (!(adc_hw->cs & ADC_CS_EN_BITS))
        adc_init();
    adc_gpio_init(PIN_CURRENT_ADC);
    fs_register(&dev_current);
}
