/**
 * boards/babywarmer_v1/board.cpp — bring the babywarmer PCB (with the
 * cpu_rp2040 board as its CPU) to a safe state: every output off first,
 * the heater above all (SSR off, safety relay released), then both I2C
 * buses and the push buttons.
 */
#include "board.h"
#include "hardware/gpio.h"
#include "drivers/buttons.h"

static void out_off(int pin, int off_level)
{
    gpio_init(pin);
    gpio_put(pin, off_level);      // level first, then direction: no glitch
    gpio_set_dir(pin, GPIO_OUT);
}

void board_led_init(void)
{
    out_off(PIN_ALARM_LED, 1);     // inverted: 1 = dark
}

void board_lamp_init(void)
{
    out_off(PIN_LAMP, !PIN_LAMP_ON);
}

void board_pins_init(void)
{
    out_off(PIN_SSR, 0);           // heater off
    out_off(PIN_HEATERSAFE, 0);    // safety relay released
    out_off(PIN_SSR_BARGRPH, 0);   // the heater device makes it PWM later
    out_off(PIN_ALARM, 0);         // buzzer silent
    board_led_init();
    board_lamp_init();
    out_off(PIN_SEG_SER, 0);
    out_off(PIN_SEG_LATCH, 0);
    out_off(PIN_SEG_SCK, 0);
    out_off(PIN_TFT_LED, 0);       // backlight off until the TFT driver starts
}

static void bus_init(i2c_inst_t *i2c, int sda, int scl, int hz)
{
    i2c_init(i2c, hz);
    gpio_set_function(sda, GPIO_FUNC_I2C);
    gpio_set_function(scl, GPIO_FUNC_I2C);
    // Pull-ups: 4k7 on cpu_rp2040 (local), on the babywarmer PCB (header).
}

void board_i2c_init(void)
{
    bus_init(BOARD_I2C, BOARD_I2C_SDA, BOARD_I2C_SCL, BOARD_I2C_HZ);
    bus_init(HEADER_I2C, HEADER_I2C_SDA, HEADER_I2C_SCL, HEADER_I2C_HZ);
}

void board_init(void)
{
    board_pins_init();
    board_i2c_init();
    buttons_init();
}
