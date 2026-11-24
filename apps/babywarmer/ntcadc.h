/**
 * apps/babywarmer/ntcadc.h — the two NTC channels of the ADS1115, sampled
 * in the background
 *
 * A kernel task ("adc", every 50 ms) keeps the ADS1115 converting, skin
 * and room in turn, and remembers the latest code of each with its time;
 * it never waits for a conversion (drivers/ads1115.h). /dev/skintemp and
 * /dev/ambient answer at once from here. Each channel is refreshed about
 * every 100 ms: ~1.3 % of the CPU in I2C transfers (tests/host/adctest).
 *
 * When the chip doesn't answer, the task waits 1 s before trying again
 * (so a broken bus can't slow the buttons, displays or console), and
 * remembers why. A reading counts as fresh for
 * 1.5 s, so one glitch never shows, and a dead chip shows within 1.5 s:
 * ntcadc_get() then fails, with the reason in errstr(). When the chip
 * answers again, readings are fresh within ~100 ms.
 *
 * The conversion: differential, +-1.024 V, 128 SPS, one
 * conversion per value.
 */
#pragma once
#include <stdint.h>

#define NTC_SKIN 0   // ADS1115 inputs 0-1
#define NTC_ROOM 1   // ADS1115 inputs 2-3
#define NTC_CHANNELS 2

// Starts the sampling (first readings now, blocking ~20 ms, then the
// "adc" task). Safe to call more than once; dev/skintemp and dev/ambient
// both do.
void ntcadc_init(void);

// The latest code of a channel, if it's fresh; else false and errstr()
// says why ("ADS1115 (0x48, header bus) not answering").
bool ntcadc_get(int ch, int16_t *code);
