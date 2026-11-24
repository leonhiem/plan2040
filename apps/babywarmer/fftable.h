/**
 * apps/babywarmer/fftable.h — the feed-forward: the heater power that holds
 * a baby at the setpoint in a given room, for the PID to correct around
 *
 * The field-tested warmer's ff_table (../warmer/warmer.cpp, LH20260427,
 * tuned on the April 2026 field logs), interpolated (safelut.h's
 * room_lookup()). prog/warmer.cpp's power in PID mode is ff + the PID's
 * correction (-35..+25 %), at most 75 %: without it the integrator would
 * have to carry the whole ~30 %, slowly. Built in: reflash to change it,
 * as in the field.
 */
#pragma once

float fftable_power(float t_room);
