/**
 * apps/babywarmer/ntclut.h — the NTC lookup table: ADS1115 code to
 * temperature, from a calibration text (TODO.md idea 3, step 3g)
 *
 * Decided with Leon 2026-11-24. The table is agnostic to the circuit
 * and the sensor: one temperature per 32 ADC codes over the whole 16-bit
 * range (2048 entries, 0.01 C each), linear in between (~0.07 C per step
 * on the babywarmer's bridge), and a marker where nothing is calibrated:
 * a reading there fails with a reason, and the guard goes to safe.
 *
 * It is made from a calibration text, the same text a person writes to
 * /dev/ntc/lut, one statement per line, `#` comments:
 *
 *   bridge 3.3 47000 47000 10000   # supply V, R above the NTC, R above / below the
 *                                  # reference point (AIN0-AIN1 = V(NTC) - V(ref))
 *   adc 1.024                      # the ADC's full scale for +-32768 (default 1.024 V)
 *   ntc 10000 25 3950              # R25, T25, Beta: a datasheet curve (no points needed)
 *   range 0 60                     # where that curve is valid (default 0 60)
 *   -6600 37.0                     # a measured point: ADC code, temperature
 *
 * The generator picks the method from what's there:
 *   - a bridge and >= 3 points: each point's code -> NTC resistance, then a
 *     Steinhart-Hart fit (1/T = A + B ln R + C ln^3 R) through the points;
 *     valid from 2 C below the coldest point to 2 C above the hottest;
 *   - a bridge and an `ntc` line (no points): the Beta curve, over `range`;
 *   - points without a bridge (>= 2): a smooth, monotonic interpolation
 *     through them in the code domain (Fritsch-Carlson), only between the
 *     first and the last point: agnostic to circuit and sensor.
 * It appends what it did as `#=` lines (method, coefficients, the error at
 * each point), so the text read back says how the table was made; `#=`
 * lines are ignored on input and made again.
 *
 * The table in use is in RAM (4 KB): at boot from the EEPROM if it holds a
 * valid one (dev/ntc.cpp), else made from the built-in text NTCLUT_BUILTIN
 * (the babywarmer bridge and the MF58 10k B3950 datasheet curve), Leon's
 * flash fallback.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define NTCLUT_STEP_LOG2  5                         // one entry per 32 codes
#define NTCLUT_ENTRIES    (65536 >> NTCLUT_STEP_LOG2)  // 2048
#define NTCLUT_NONE       INT16_MIN                 // "not calibrated here"
#define NTCLUT_TEXT_MAX   1024                      // the calibration text, as a /fram file

// The built-in calibration: the babywarmer's bridge (3V3, 47k/47k/10k) and
// the MF58 10k B3950 datasheet curve.
#define NTCLUT_BUILTIN \
    "# built-in: the babywarmer bridge (3V3, 47k / 47k + 10k) and the\n" \
    "# MF58 10k B3950 datasheet curve, until the EEPROM has a calibration\n" \
    "bridge 3.3 47000 47000 10000\n" \
    "ntc 10000 25 3950\n" \
    "range -5 60\n"

typedef struct {
    int16_t t[NTCLUT_ENTRIES];        // 0.01 C per entry, NTCLUT_NONE where not calibrated
} ntclut_t;

// Makes a table from a calibration text. On success fills *lut, and writes
// the text as it will be stored (the input without its old `#=` lines, plus
// new ones saying what was done) to out (at most outlen, NUL-terminated).
// On failure returns false; errstr() says why ("line 4: ...").
bool ntclut_make(const char *text, int len, ntclut_t *lut, char *out, int outlen);

// The temperature for an ADC code, interpolated; false (and errstr) where
// the table isn't calibrated.
bool ntclut_lookup(const ntclut_t *lut, int16_t code, float *temp);
