#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "hardware/gpio.h"   // as the SDK's pico/stdlib.h
typedef uint64_t absolute_time_t;
#define PICO_ERROR_TIMEOUT (-1)
extern uint64_t fake_now;
static inline absolute_time_t get_absolute_time(void){ return fake_now; }
static inline absolute_time_t make_timeout_time_ms(uint32_t ms){ return fake_now + (uint64_t)ms*1000; }
static inline void sleep_ms(uint32_t ms){ fake_now += (uint64_t)ms*1000; }
static inline void sleep_us(uint64_t us){ fake_now += us; }
static inline absolute_time_t make_timeout_time_us(uint64_t us){ return fake_now + us; }
static inline int64_t absolute_time_diff_us(absolute_time_t a, absolute_time_t b){ return (int64_t)(b-a); }
static inline bool time_reached(absolute_time_t t){ return fake_now >= t; }
static inline uint32_t to_ms_since_boot(absolute_time_t t){ return (uint32_t)(t / 1000); }
int getchar_timeout_us(uint32_t us);
// A repeating timer: the test calls fake_timer_cb itself (btntest).
struct repeating_timer { int unused; };
typedef bool (*repeating_timer_callback_t)(struct repeating_timer *);
extern repeating_timer_callback_t fake_timer_cb;
static inline bool add_repeating_timer_ms(int32_t, repeating_timer_callback_t cb, void *, struct repeating_timer *)
{ fake_timer_cb = cb; return true; }
