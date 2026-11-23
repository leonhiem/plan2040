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
