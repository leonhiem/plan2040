/**
 * apps/babywarmer/dev/current.cpp — /dev/current: the heater current sense
 *
 *   cat /dev/current          raw ADC counts 0-4095 (ADC0), the average of 8
 *
 * Raw counts: the circuit has no documented conversion to amps. The guard
 * compares it with the SSR's state. The ADC block is started only if
 * nothing did yet: adc_init() would switch off /dev/cputemp's sensor.
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
