/**
 * apps/babywarmer/heater.h — the heater output: time-proportional SSR
 * switching (TPO) and the PWM bargraph
 *
 * From picoos @ 89c16e5 (heater.cpp + the TPO/PWM constants of
 * warmer.h). heater_set_power() takes 0-100 %; tpo_apply(), a kernel
 * task every TPO_INTERVAL_TIME ms (registered by dev/heater.cpp), turns
 * it into SSR on/off over a 15 s window and sets the bargraph.
 */
#pragma once

#define TPO_PERIOD_MS     15000  // 15-second window
#define TICK_MS           100    // tick used by TPO_TICKS
#define TPO_TICKS         (TPO_PERIOD_MS / TICK_MS)   // = 150 ticks
#define TPO_INTERVAL_TIME 100    // 100 ms: how often tpo_apply() ticks

#define PWM_SLICE_NUM 0          // GPIO16 (PIN_SSR_BARGRPH) = PWM slice 0, channel A
#define PWM_WRAP_VAL  41666      // top; with clkdiv=1 -> 125e6/1/41667 = 3000.01 Hz (was 50Hz/0xFFFF)

void  heater_pwm_init(void);     // GPIO16 to PWM, level 0 (picoos: in setup_gpios())
void  tpo_apply(void);
void  heater_set_power(float pct); // clamped to [0,100]
float heater_get_power(void);      // last commanded value
