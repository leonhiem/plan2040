#pragma once
#include <stdint.h>
typedef uint64_t absolute_time_t;
#define PICO_ERROR_TIMEOUT (-1)
extern uint64_t fake_now;
static inline absolute_time_t get_absolute_time(void){ return fake_now; }
static inline absolute_time_t make_timeout_time_ms(uint32_t ms){ return fake_now + (uint64_t)ms*1000; }
static inline int64_t absolute_time_diff_us(absolute_time_t a, absolute_time_t b){ return (int64_t)(b-a); }
int getchar_timeout_us(uint32_t us);
