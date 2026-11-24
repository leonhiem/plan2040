#pragma once
// Fake RP2040 GPIO for the PC tests: levels in fake_gpio[], and a hook
// called on every gpio_put() (outtest uses it to catch the 74HC595 shift-out).
#include <stdint.h>
#include <stdbool.h>   // the TFT code is C
typedef unsigned int uint;
extern int fake_gpio[30];
extern void (*fake_gpio_hook)(uint pin, int value);
#define GPIO_OUT 1
#define GPIO_IN  0
enum gpio_function { GPIO_FUNC_SPI = 1, GPIO_FUNC_I2C = 3, GPIO_FUNC_PWM = 4, GPIO_FUNC_SIO = 5 };
static inline void gpio_put(uint pin, bool v) { fake_gpio[pin] = v; if (fake_gpio_hook) fake_gpio_hook(pin, v); }
static inline bool gpio_get(uint pin) { return fake_gpio[pin]; }
static inline void gpio_init(uint) {}
static inline void gpio_pull_up(uint) {}
static inline void gpio_set_dir(uint, bool) {}
static inline void gpio_set_function(uint, enum gpio_function) {}
