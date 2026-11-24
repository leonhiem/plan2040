/**
 * apps/babywarmer/fftable.h — the feed-forward: the heater power that holds
 * a baby at the setpoint in a given room, for the PID to correct around
 *
 * The field-tested warmer's ff_table (../warmer/warmer.cpp, LH20260427,
 * tuned on the April 2026 field logs). In PID mode the power is ff + the
 * PID's correction (-35..+25 %), at most 75 % (prog/pid.cpp with a bias):
 * without it the integrator would have to carry the whole ~30 %, slowly.
 *
 * Built in, exactly as the safe table (safelut.h): reflash to change it,
 * as in the field. Fed to the PID through /dev/ffpower (dev/phase.cpp):
 *
 *   cat /dev/ambient | fftable > /dev/ffpower &
 */
#pragma once

float fftable_power(float t_room);
