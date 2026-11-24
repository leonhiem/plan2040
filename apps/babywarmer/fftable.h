/**
 * apps/babywarmer/fftable.h — the feed-forward: the heater power that holds
 * a baby at the setpoint in a given room, for the PID to correct around
 *
 * The field-tested warmer's ff_table (../warmer/warmer.cpp, LH20260427,
 * tuned on Leon's logs in a real room in April 2026), back in plan2040's
 * step 5 (TODO.md idea 3, 2026-11-24): picoos had cut it ("PID alone"), so
 * the PID's integrator had to carry the whole ~27-30 % by itself, slowly.
 * In PID mode the power is ff + the PID's correction (-35..+25 %), at most
 * 75 % (prog/pid.cpp with a bias).
 *
 * Built in, exactly as the safe table (safelut.h): reflash to change it,
 * as in the field. Fed to the PID through /dev/ffpower (dev/phase.cpp):
 *
 *   cat /dev/ambient | fftable > /dev/ffpower &
 */
#pragma once

float fftable_power(float t_room);
