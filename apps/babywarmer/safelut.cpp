// apps/babywarmer/safelut.cpp — see safelut.h. The table and lookup()
// from picoos @ 89c16e5 (prog/safelut.cpp), unchanged.
#include "safelut.h"

static const room_point_t safe_ff_table[] = {
    { 10.0f, 65.0f },
    { 14.0f, 58.0f },
    { 18.0f, 51.0f },
    { 20.0f, 47.0f },
    { 22.0f, 43.0f },
    { 24.0f, 39.0f },
    { 26.0f, 35.0f },
    { 28.0f, 32.0f },
    { 30.0f, 29.0f },
    { 32.0f, 25.0f },
    { 33.0f, 22.0f },
};
#define SAFE_FF_LEN ((int)(sizeof(safe_ff_table) / sizeof(safe_ff_table[0])))

float room_lookup(const room_point_t *table, int len, float t_room)
{
    if (t_room <= table[0].t_room) return table[0].pwr_pct;
    if (t_room >= table[len - 1].t_room) return table[len - 1].pwr_pct;

    for (int i = 0; i < len - 1; i++) {
        if (t_room < table[i + 1].t_room) {
            float span = table[i + 1].t_room - table[i].t_room;
            float frac = (t_room - table[i].t_room) / span;
            return table[i].pwr_pct + frac * (table[i + 1].pwr_pct - table[i].pwr_pct);
        }
    }
    return table[len - 1].pwr_pct; // unreachable given the bounds checks above
}

float safelut_power(float t_room)
{
    float pwr = room_lookup(safe_ff_table, SAFE_FF_LEN, t_room);
    if (pwr > DUTY_SAFE_CAP) pwr = DUTY_SAFE_CAP;
    if (pwr < 0.0f)          pwr = 0.0f;
    return pwr;
}
