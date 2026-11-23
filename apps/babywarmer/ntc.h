/**
 * apps/babywarmer/ntc.h — ADS1115 code to degrees C, from the NTC table in
 * use (dev/ntc.cpp: the EEPROM's calibration, or the built-in MF58 table)
 *
 * Since 2026-11-24 (step 3g). picoos's ntc_convert() and ntc_lut.h (a
 * fixed table, recalibrated 2026-08-30 for a substitute NTC that wasn't
 * an MF58, with a wrap-around above code 21742) are gone; outside the
 * calibrated range a reading now fails with a reason instead.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

// The temperature for an ADC code, or false (errstr: "code ... is outside
// the calibrated range (open or shorted?)").
bool ntc_temp(int16_t code, float *t);
