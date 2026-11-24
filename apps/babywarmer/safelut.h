/**
 * apps/babywarmer/safelut.h — the heater power that keeps a baby warm at a
 * given room temperature, without a skin reading (safe mode)
 *
 * babywarmer's own safe_ff_table (heater.cpp's safe_feedforward(),
 * recovered verbatim in picoos's prog/safelut.cpp), interpolated
 * linearly, capped at DUTY_SAFE_CAP (55 %). Shared since 2026-11-24 by
 * the `safelut` program and the guard (dev/guard.cpp), which must not
 * depend on a job for its fallback power.
 */
#pragma once

#define DUTY_SAFE_CAP 55.0f

float safelut_power(float t_room);

// The interpolation both room tables use (safelut, fftable.h): linear
// between the points, the end values beyond them.
typedef struct { float t_room; float pwr_pct; } room_point_t;
float room_lookup(const room_point_t *table, int len, float t_room);
