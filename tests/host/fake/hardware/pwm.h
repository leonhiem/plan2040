#pragma once
// Fake RP2040 PWM for the PC tests: the last level per slice/channel.
#include <stdint.h>
typedef struct { uint32_t wrap; } pwm_config;
extern uint16_t fake_pwm_level[8][2];
#define PWM_CHAN_A 0
#define PWM_CHAN_B 1
static inline pwm_config pwm_get_default_config(void) { pwm_config c = { 0xffff }; return c; }
static inline void pwm_config_set_clkdiv_int(pwm_config *, uint32_t) {}
static inline void pwm_config_set_wrap(pwm_config *c, uint16_t w) { c->wrap = w; }
static inline void pwm_init(unsigned, pwm_config *, bool) {}
static inline void pwm_set_chan_level(unsigned slice, unsigned chan, uint16_t level) { fake_pwm_level[slice][chan] = level; }
static inline void pwm_set_enabled(unsigned, bool) {}
