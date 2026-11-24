/**
 * apps/babywarmer/safelut.h — the safe mode's heater power for a room
 * temperature: the field warmer's safe_ff_table, interpolated, at most
 * DUTY_SAFE_CAP (55 %)
 *
 * Used by the guard (dev/guard.cpp) when the skin reading can't be trusted,
 * and by the `safelut` program for a look from the shell. room_lookup() is
 * the interpolation fftable.h uses too.
 */
#pragma once

#define DUTY_SAFE_CAP 55.0f

float safelut_power(float t_room);

// The interpolation both room tables use (safelut, fftable.h): linear
// between the points, the end values beyond them.
typedef struct { float t_room; float pwr_pct; } room_point_t;
float room_lookup(const room_point_t *table, int len, float t_room);
