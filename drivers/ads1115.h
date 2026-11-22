/**
 * drivers/ads1115.h — TI ADS1115, 16-bit 4-channel I2C ADC
 *
 * Written for plan2040 from the TI datasheet (SBAS444), 2026-11-23,
 * replacing the copied pico-ads1115 library (Antonio González, BSD-3),
 * which picoos used: thanks to it for getting the babywarmer's NTCs read
 * in the first place. Why our own: TODO.md idea 3, step 3e; mainly, the
 * library waited for every conversion (~8 ms at 128 SPS with the CPU
 * busy), and plan2040 drivers never block (drivers/README.md).
 *
 * The chip: two registers that matter, conversion (0x00, the last
 * result, signed 16-bit) and config (0x01). Writing config with bit 15
 * (OS) set starts one conversion in single-shot mode; reading config
 * back, OS = 1 means "not converting", i.e. the result is ready. A
 * conversion takes 1/rate: 7.8 ms at 128 SPS.
 *
 * So the use is in three non-blocking steps, spread over ticks:
 *
 *   ads1115_start(ADS1115_MUX_DIFF_0_1, ADS1115_PGA_1_024, ADS1115_RATE_128)
 *   ... a later tick: ads1115_ready() == 1 ?
 *   ads1115_result(&code)
 *
 * Each step is one or two short I2C transfers (~0.1-0.3 ms at 200 kHz),
 * with a timeout. Each returns false (ready(): -1) when the chip doesn't
 * answer, with the reason in errstr(): "ADS1115 (0x48, header bus) not
 * answering". ads1115_convert() does all three and waits, for bringup
 * and boot-time use only (it may block ~10 ms).
 *
 * Bus and address from board.h: ADS1115_I2C, ADS1115_I2C_ADDR,
 * ADS1115_I2C_NAME ("header bus").
 */
#pragma once

#include <stdint.h>

// Inputs (config bits 14:12): differential pairs and single-ended.
#define ADS1115_MUX_DIFF_0_1  0x0
#define ADS1115_MUX_DIFF_0_3  0x1
#define ADS1115_MUX_DIFF_1_3  0x2
#define ADS1115_MUX_DIFF_2_3  0x3
#define ADS1115_MUX_SINGLE_0  0x4
#define ADS1115_MUX_SINGLE_1  0x5
#define ADS1115_MUX_SINGLE_2  0x6
#define ADS1115_MUX_SINGLE_3  0x7

// Full-scale range (bits 11:9).
#define ADS1115_PGA_6_144     0x0
#define ADS1115_PGA_4_096     0x1
#define ADS1115_PGA_2_048     0x2   // the chip's power-up default
#define ADS1115_PGA_1_024     0x3
#define ADS1115_PGA_0_512     0x4
#define ADS1115_PGA_0_256     0x5   // 6 and 7 are +-0.256 V too

// Samples per second (bits 7:5): a conversion takes 1/rate.
#define ADS1115_RATE_8        0x0
#define ADS1115_RATE_16       0x1
#define ADS1115_RATE_32       0x2
#define ADS1115_RATE_64       0x3
#define ADS1115_RATE_128      0x4   // the chip's power-up default, 7.8 ms
#define ADS1115_RATE_250      0x5
#define ADS1115_RATE_475      0x6
#define ADS1115_RATE_860      0x7

bool ads1115_start(int mux, int pga, int rate);  // start one conversion
int  ads1115_ready(void);                        // 1 done, 0 still converting, -1 no answer
bool ads1115_result(int16_t *code);              // the last conversion's result

// start + wait + result, at most ~10 ms longer than one conversion.
// Blocks: for bringup and for a first reading at boot, never in a tick.
bool ads1115_convert(int mux, int pga, int rate, int16_t *code);
