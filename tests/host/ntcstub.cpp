// For tests that read /dev/skintemp without dev/ntc.cpp (adctest): the
// built-in table, as dev/ntc.cpp uses it while the EEPROM has none.
#include "ntc.h"
#include "ntclut.h"
#include <cstring>
bool ntc_temp(int16_t code, float *t)
{
    static ntclut_t lut; static char text[2048]; static bool made;
    if (!made) { ntclut_make(NTCLUT_BUILTIN, strlen(NTCLUT_BUILTIN), &lut, text, sizeof text); made = true; }
    return ntclut_lookup(&lut, code, t);
}
