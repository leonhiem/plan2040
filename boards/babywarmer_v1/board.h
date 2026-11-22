/**
 * boards/babywarmer_v1/board.h — the cpu_rp2040 board as the CPU of the
 * old babywarmer PCB
 *
 * Leon's hardware hack (2026-11-21, TODO.md idea 3): the babywarmer
 * PCB's own RP2040 is not used; the cpu_rp2040 board is wired in its
 * place, the way it will sit on the future carrier board. So this is
 * cpu_rp2040 (local bus with FRAM + RTC) plus the babywarmer's pin map
 * (../picoos/warmer.h, unchanged since 2026-08) plus the babywarmer's
 * I2C bus with the ADS1115 and Leon's added 24C512 EEPROM.
 *
 * Two I2C buses:
 *   local  i2c1  GPIO14/15  FM24CL64B FRAM 0x50, PCF8563T RTC 0x51  (on cpu_rp2040)
 *   header i2c0  GPIO12/13  ADS1115 ADC 0x48, 24C512 EEPROM 0x52     (on the babywarmer PCB)
 *
 * Before wiring in: take the test LEDs off cpu_rp2040's GPIO1 and GPIO6
 * (they'd sit in parallel with the babywarmer's alarm LED and lamp
 * driver), and keep the babywarmer's own RP2040 from running (removed,
 * or RUN held low), or it drives the same outputs.
 */
#pragma once

#include "hardware/i2c.h"

// ------------------------------------------------------------ I2C
// Local bus, on the cpu_rp2040 board: as in boards/cpu_rp2040/board.h.
#define BOARD_I2C          i2c1
#define BOARD_I2C_SDA      14
#define BOARD_I2C_SCL      15
#define BOARD_I2C_HZ       400000

// Header bus: the babywarmer PCB's ADS1115, plus the EEPROM next to it
// (the same place it will have on the future ADC module). 200 kHz as
// picoos ran it on this PCB; longer wires than the local bus.
#define HEADER_I2C         i2c0
#define HEADER_I2C_SDA     12
#define HEADER_I2C_SCL     13
#define HEADER_I2C_HZ      200000

// Each chip: its bus and address.
#define RTC_I2C            BOARD_I2C
#define RTC_I2C_ADDR       0x51  // PCF8563T, fixed address
#define FRAM_I2C           BOARD_I2C
#define FRAM_I2C_ADDR      0x50  // FM24CL64B, A2..A0 = 0
#define ADS1115_I2C        HEADER_I2C
#define ADS1115_I2C_ADDR   0x48  // ADDR pin to GND
#define ADS1115_I2C_NAME   "header bus"   // for error texts
#define EEPROM_I2C         HEADER_I2C
#define EEPROM_I2C_ADDR    0x52  // 24C512, A2 A1 A0 = 010
#define EEPROM_SIZE        65536
#define EEPROM_PAGE        128   // 24C512 (a 24C256: 32768 / 64)

// FRAM layout: the same chip and layout as cpu_rp2040 (README "Files on
// the FRAM"): filesystem, then the 256-byte system area mkfs never touches.
#define FRAM_SIZE          8192
#define SYSAREA_SIZE       256
#define SYSAREA_ADDR       (FRAM_SIZE - SYSAREA_SIZE)

// ------------------------------------------------------------ outputs
// All set to "off" first thing at boot (board_pins_init()). Polarities
// from picoos's dev/*.cpp on this PCB.
#define PIN_ALARM          0   // buzzer, 1 = sounding (since a 2026-08-26 hardware change)
#define PIN_ALARM_LED      1   // alarm LED, inverted: 0 = lit
#define PIN_SEG_SER        3   // 7-segment shift register: data
#define PIN_SEG_LATCH      4   //                           latch
#define PIN_SEG_SCK        5   //                           clock
#define PIN_LAMP           6   // baby light, active-high
#define PIN_LAMP_ON        1
#define PIN_SSR            11  // heater SSR, 1 = on
#define PIN_SSR_BARGRPH    16  // heater bargraph, PWM (slice 0 A)
#define PIN_HEATERSAFE     17  // mechanical safety relay, 1 = energized (heater may run)

// ST7735 TFT on spi0 (drivers/st7735, step 3f).
#define PIN_TFT_LED        20  // backlight
#define PIN_TFT_RST        21
#define PIN_TFT_SCK        22
#define PIN_TFT_MOSI       23
#define PIN_TFT_DC         24
#define PIN_TFT_CS         25

// ------------------------------------------------------------ inputs
#define PIN_CURRENT_ADC    26  // heater current sense, ADC0

// Push buttons, active low, internal pull-ups. Same as cpu_rp2040.
#define BUTTON_PIN_MUTE    2
#define BUTTON_PIN_UP      7
#define BUTTON_PIN_DOWN    8
#define BUTTON_PIN_MANUAL  9
#define BUTTON_PIN_START  28
#define BUTTON_PIN_LAMP   29

// GPIO 10, 18, 19, 27: free.

// boards/babywarmer_v1/board.cpp
void board_init(void);       // plan2040: board_pins_init(), board_i2c_init(), buttons_init()
void board_pins_init(void);  // every output configured and off (bringup uses this too)
void board_i2c_init(void);   // both buses
void board_led_init(void);   // configured, off
void board_lamp_init(void);  // configured, off
