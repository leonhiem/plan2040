/**
 * apps/babywarmer/seg7.h — the front panel's 74HC595 chain: two 3-digit
 * 7-segment displays and 7 LEDs, 56 bits in one shift-out
 *
 * dev/seg7.cpp is the only user. Pins from board.h (PIN_SEG_SER/SCK/LATCH).
 */
#pragma once
#include <stdint.h>

// The 7 front-panel LEDs, one bit each.
typedef struct led_s {
    uint8_t aut;
    uint8_t warm;
    uint8_t low;
    uint8_t high;
    uint8_t fail;
    uint8_t chk;
    uint8_t man;
} led_t;

// Digit values for update_7seg()'s dig* args: 0-9 are literal digits.
#define SEG_BLANK 10
#define SEG_ALL   11

void update_7seg(uint8_t digl3, uint8_t digl2, uint8_t digl1, uint8_t dotl,
                 uint8_t digs3, uint8_t digs2, uint8_t digs1, uint8_t dots, led_t led);
void clear_seg7(void);
void init_seg7(void);   // lamp test: everything on for 1 s, then blank
