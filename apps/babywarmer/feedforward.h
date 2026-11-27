/**
 * apps/babywarmer/feedforward.h — the heater power that holds the skin at
 * the setpoint in a given room
 *
 *   power = FF_K * (setpoint - room), within 0..100 %
 *
 * The heat a baby loses grows with the difference between its skin and
 * the room, so the power that holds it does too. Used by prog/warmer.cpp
 * (power = this + the PID's correction) and the guard (safe: 90 % of it,
 * heating without a trusted skin reading).
 *
 * FF_K from the logged warm-ups on the board: 3.5 % per C (2026-11-27,
 * with the room NTC moved out of the metal pole, which had read 1-2 C high
 * while the warmer ran: 48 % held 37.3 C in a 23.4 C room; the first
 * warm-up, 47.5 % at 37.0 C, gives 3.5 against the real room too; 3.7
 * until then, fitted against the warm pole). It replaces the field warmer's two
 * room-only tables (feed-forward and safe), which had no setpoint in them:
 * at 37.0 C they gave 30 % and 38 %, the PID's integral had to make up
 * 18 % slowly (the skin stuck at 35.5 C for 10 minutes), and safe mode
 * held the skin 2.7 C under the setpoint, too low to ever recover.
 */
#pragma once

#define FF_K 3.5f   // % per C of (setpoint - room)

static inline float feedforward_power(float setpoint, float t_room)
{
    float p = FF_K * (setpoint - t_room);
    return p < 0.0f ? 0.0f : p > 100.0f ? 100.0f : p;
}
