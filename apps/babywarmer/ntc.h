/**
 * apps/babywarmer/ntc.h — ADS1115 code to degrees C, from the NTC table in
 * use (dev/ntc.cpp: the EEPROM's calibration, or the built-in MF58 table).
 * Outside the calibrated range a reading fails, with the reason.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

// The temperature for an ADC code, or false (errstr: "code ... is outside
// the calibrated range (open or shorted?)").
bool ntc_temp(int16_t code, float *t);
