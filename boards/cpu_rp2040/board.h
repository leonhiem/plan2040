/**
 * boards/cpu_rp2040/board.h — cpu_rp2040 PCB pin map and on-board peripherals
 *
 * One directory per board under boards/; the config (configs/<name>.cmake)
 * picks the board, and the build puts that board's directory on the include
 * path, so every file just says #include "board.h".
 *
 * The general-purpose RP2040 board derived from the babywarmer PCB:
 * RP2040, power supply, crystal, QSPI flash (same size as babywarmer),
 * CD4093/CD4013 on/off logic (pure hardware, no GPIO involved), USB,
 * plus a PCF8563T RTC and an FM24CL64B FRAM sharing one I2C bus.
 *
 * Button and LED GPIOs match the babywarmer board (../picoos/warmer.h),
 * except START and LAMP (2026-11-25): GPIO28/29 stay free for the 9P UART
 * to other boards (UART0 TX 28, RX 29).
 */
#pragma once

#include "hardware/i2c.h"

// On-board I2C bus: RTC + FRAM. 4k7 pull-ups on the board. Local to this
// board, NOT exported on the headers. (The headers export GPIO12 = SDA and
// GPIO13 = SCL, i2c0, for the carrier board's ADS1115 -- a separate bus.)
#define BOARD_I2C          i2c1
#define BOARD_I2C_SDA      14
#define BOARD_I2C_SCL      15
#define BOARD_I2C_HZ       400000 // PCF8563's max; bring-up first passed at 100k

// Each chip: its bus and address. Drivers take them from here, so a
// chip can sit on another bus on another board (boards/babywarmer_v1).
#define RTC_I2C            BOARD_I2C
#define RTC_I2C_ADDR       0x51  // PCF8563T, fixed address
#define RTC_I2C_NAME       "local bus"    // for error texts
#define FRAM_I2C           BOARD_I2C
#define FRAM_I2C_ADDR      0x50  // FM24CL64B, A2..A0 = 0
#define FRAM_I2C_NAME      "local bus"
#define FRAM_SIZE          8192  // 64 Kbit; WP hardwired to GND

// FRAM layout (decided 2026-11-15, see README "Files on the FRAM"):
//   0 .. SYSAREA_ADDR-1        filesystem (fs/framfs.h), wiped by mkfs
//   SYSAREA_ADDR .. FRAM_SIZE  system area (fs/hours.h): hour counters,
//                              NEVER touched by mkfs
#define SYSAREA_SIZE       256
#define SYSAREA_ADDR       (FRAM_SIZE - SYSAREA_SIZE)

// PCF8563 notes (details in README.md, "Hardware notes"):
// - INT and CLKOUT: pads only, not wired to a GPIO yet.
// - CLKOUT defaults ON (32.768 kHz): turn it off at init (reg 0x0D = 0x00),
//   it only drains the backup supercap.
// - control_2 bits 7..6 are unused and power up random (seen 0x80/0xc0):
//   write control_2 explicitly at init.
// - Seconds bit 7 = VL: time not trusted (VDD fell below ~0.9 V). Writing
//   the time clears it.
// - Backup: 10 F / 3 V supercap on VDD, charged from 3V3 via 220R + 1N4007.
//   Clock runs down to ~1.0 V.

// LED: same as babywarmer's alarm LED, inverted in hardware (0 = lit).
// The GPIO sinks the LED: anode -> 3V3, cathode -> 220R -> GPIO1.
//
// LESSON (2026-11-11): the anode was first soldered to +5V. That "works"
// (LED blinks), but RP2040 GPIOs are NOT 5V tolerant (abs max IOVDD+0.5V):
// whenever the pin is high or unconfigured (every reset/boot), 5V leaks
// through the LED into the pin's ESD diode, back into 3V3. Any external
// load on a GPIO goes to 3V3, never to +5V -- use a transistor/MOSFET if
// something really needs 5V. Red LED at 3V3/220R is ~7mA, so the pin's
// drive strength is raised to 8mA (default is 4mA).
#define PIN_ALARM_LED      1

// Lamp output, GPIO6 like babywarmer's baby-light. babywarmer drove it
// active-high (1 = on); on this board a test LED is wired the same way
// as the one on GPIO1 (3V3 -> LED -> resistor -> GPIO6), so 0 = lit.
// Flip PIN_LAMP_ON to 1 for an active-high load (e.g. a transistor).
//
// LESSON (2026-11-12): before picoos configured GPIO6, that LED glowed
// dimly with 1.5 V on the pin -- an unconfigured RP2040 pin is an input
// with a ~50k internal pull-down, enough to pass a few tens of uA through
// an LED wired to 3V3. Outputs like this get set to "off" first thing at
// boot (board_lamp_init()).
#define PIN_LAMP           6
#define PIN_LAMP_ON        0

// Push buttons, active low, internal pull-ups. START and LAMP were on
// GPIO28/29 (the babywarmer PCB's); moved for the 9P UART, which needs those.
#define BUTTON_PIN_MUTE    2
#define BUTTON_PIN_UP      7
#define BUTTON_PIN_DOWN    8
#define BUTTON_PIN_MANUAL  9
#define BUTTON_PIN_START  10
#define BUTTON_PIN_LAMP   27

// UART0: the link to another board, /dev/eia0 (drivers/eia.cpp), for 9P
// later (decided 2026-11-24, plan2040_ideas/IDEAS.md section 1). Crossed
// to the other board, a null modem: TX -> RX, RTS -> CTS both ways, plus
// GND. RTS/CTS let the receiver stop the sender in hardware when it can't
// keep up. CTS and RX get the internal pull-up, so a loose wire reads as
// "idle" and "not clear to send" rather than noise.
//
// LESSON (2026-11-25): while the other board resets (or sits in BOOTSEL),
// its TX pin is an input with the RP2040's ~50k pull-down, against our
// ~50k pull-up: the line floats at half supply and reads as a stray byte
// or a break. Next board: a 10k pull-up to 3V3 on each RX (and CTS), and
// ~1k in series with each TX (and RTS): an RP2040 pin isn't fail-safe,
// so a TX driving an unpowered board would feed it through its ESD diode
// (the +5V LED lesson again); 1k keeps that to a few mA, and at 1 Mbaud
// (1 us a bit) its RC with the wire's capacitance is far too small to
// matter. Not needed on the bench: both boards power up together.
#define EIA0_UART          uart0
#define EIA0_NAME          "UART0"
#define EIA0_PIN_TX        28
#define EIA0_PIN_RX        29
#define EIA0_PIN_CTS       18
#define EIA0_PIN_RTS       19
#define EIA0_BAUD          1000000   // exact: 125 MHz / (16 x 7.8125)

// boards/cpu_rp2040/board.cpp. Every board has these three:
void board_init(void);       // plan2040: board_pins_init(), board_i2c_init(), buttons_init()
void board_pins_init(void);  // every output configured and off (bringup uses this too)
void board_i2c_init(void);   // every I2C bus of the board
void board_led_init(void);   // configured, off
void board_lamp_init(void);  // configured, off
