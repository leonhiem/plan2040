#include "ntcadc.h"
#include "board.h"
#include "drivers/ads1115.h"
#include "kernel/errstr.h"
#include "kernel/task.h"
#include "pico/stdlib.h"
#include <cstdio>

#define ADC_TICK_MS     50     // a conversion per tick: each channel every ~100 ms
#define RETRY_MS        1000   // after a failure: wait this long before the next try
#define FRESH_MS        1500   // a reading older than this doesn't count
#define CONV_LIMIT_MS   100    // one conversion takes 7.8 ms; this long means it's stuck

#define PGA  ADS1115_PGA_1_024
#define RATE ADS1115_RATE_128

static const int mux[NTC_CHANNELS] = { ADS1115_MUX_DIFF_0_1, ADS1115_MUX_DIFF_2_3 };

static bool     started;
static int16_t  code[NTC_CHANNELS];
static bool     have[NTC_CHANNELS];
static absolute_time_t when[NTC_CHANNELS];
static int      ch;                     // the channel being converted
static bool     converting;
static absolute_time_t conv_start, retry_at;
static char     reason[ERRMAX];         // why the chip failed last, "" while it works

static void failed(void)
{
    snprintf(reason, sizeof(reason), "%s", errstr());
    converting = false;
    retry_at = make_timeout_time_ms(RETRY_MS);
}

static bool start(void)
{
    if (!ads1115_start(mux[ch], PGA, RATE)) { failed(); return false; }
    converting = true;
    conv_start = get_absolute_time();
    return true;
}

static void adc_task(void)
{
    if (!time_reached(retry_at)) return;           // backing off after a failure
    if (!converting) { start(); return; }

    int r = ads1115_ready();
    if (r < 0) { failed(); return; }
    if (r == 0) {
        if (absolute_time_diff_us(conv_start, get_absolute_time()) > CONV_LIMIT_MS * 1000) {
            werrstr("ADS1115 (0x%02x, %s): conversion never finished", ADS1115_I2C_ADDR, ADS1115_I2C_NAME);
            failed();
        }
        return;                                    // still converting: next tick
    }
    int16_t v;
    if (!ads1115_result(&v)) { failed(); return; }
    code[ch] = v;
    have[ch] = true;
    when[ch] = get_absolute_time();
    reason[0] = '\0';
    ch = (ch + 1) % NTC_CHANNELS;
    start();                                       // the next channel at once
}

void ntcadc_init(void)
{
    if (started) return;
    started = true;
    // First readings now, so the boot script's jobs find values (blocking,
    // at boot only: ~9 ms per channel, or a quick failure without a chip).
    for (int c = 0; c < NTC_CHANNELS; c++) {
        int16_t v;
        if (!ads1115_convert(mux[c], PGA, RATE, &v)) { failed(); break; }
        code[c] = v;
        have[c] = true;
        when[c] = get_absolute_time();
    }
    task_register("adc", adc_task, ADC_TICK_MS);
}

bool ntcadc_get(int c, int16_t *out)
{
    if (c < 0 || c >= NTC_CHANNELS) { werrstr("no such channel"); return false; }
    if (have[c] && absolute_time_diff_us(when[c], get_absolute_time()) <= FRESH_MS * 1000) {
        *out = code[c];
        return true;
    }
    if (reason[0])      werrstr("%s", reason);
    else if (!have[c])  werrstr("no reading yet");
    else                werrstr("no new reading for %lld ms",
                                absolute_time_diff_us(when[c], get_absolute_time()) / 1000);
    return false;
}
