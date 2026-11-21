/**
 * apps/babywarmer/ntc.h — ADS1115 code to degrees Celsius, for the NTC
 * channels (skin, ambient)
 *
 * From picoos @ 89c16e5 (ntc.h/ntc.cpp), unchanged. The table in
 * ntc_lut.h is a plain array (no static/extern), so it's included from
 * ntc.cpp only: including it twice was a "multiple definition" link
 * error in picoos once dev/ambient.cpp needed it too.
 *
 * The table: 650 entries, one per 32 ADC codes, covering codes -11025
 * .. 9743 (+-1.024 V range): the 2026-08-30 recalibration for a
 * substitute NTC, see the comment at the top of ntc_lut.h.
 *
 * NOTE (2026-11-21, TODO idea 3): codes above 21742 wrap around in the
 * (short) arithmetic and land on entry 0, the hottest value (91.8 C),
 * while codes 9744..21742 clamp to the coldest, entry 649 (22.9 C). And
 * nothing reads below 22.9 C: a colder room reads 22.9, a 10 C low alarm
 * can't fire. Kept as in picoos; Leon: the table is experimental, the
 * full EEPROM lookup table (step 3g) replaces it and fixes both.
 */
#pragma once
#include <stdint.h>

float ntc_convert(uint16_t adc_value);
