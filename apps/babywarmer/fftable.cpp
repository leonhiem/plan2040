// apps/babywarmer/fftable.cpp — see fftable.h. The table is warmer.cpp's
// ff_table (LH20260427), unchanged, with its tuning notes.
#include "fftable.h"
#include "safelut.h"   // room_lookup()

static const room_point_t ff_table[] = {
    // "Bumped Apr 2026 (round 2): logged steady-state heater at amb=27.7 C
    // was 27.3 % with FF=22; bumping ~3 % more closes most of the residual
    // gap. Integrator now expected to settle around +2 to +3 %."
    { 10.0f, 50.0f },   // winter northern Vietnam (conservative)
    { 12.0f, 48.0f },
    { 14.0f, 46.0f },
    { 16.0f, 43.0f },
    { 18.0f, 40.0f },
    { 20.0f, 37.0f },
    { 22.0f, 34.0f },   // extrapolated
    { 24.0f, 30.0f },   // extrapolated
    { 26.0f, 28.0f },
    { 28.0f, 25.0f },   // true steady state ~27 %, leaves ~2 % for the integrator
    { 30.0f, 22.0f },
    { 32.0f, 18.0f },
    { 33.0f, 15.0f },
};
#define FF_LEN ((int)(sizeof(ff_table) / sizeof(ff_table[0])))

float fftable_power(float t_room)
{
    return room_lookup(ff_table, FF_LEN, t_room);
}
