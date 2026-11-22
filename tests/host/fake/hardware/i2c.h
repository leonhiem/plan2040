#pragma once
// Fake: enough for a real board.h to compile on the PC, and the two I2C
// calls plan2040's drivers use; a test that runs a driver implements them
// (adctest: a fake ADS1115).
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef struct i2c_inst i2c_inst_t;
extern i2c_inst_t *fake_i2c0, *fake_i2c1;
#define i2c0 fake_i2c0
#define i2c1 fake_i2c1
#define PICO_ERROR_GENERIC (-2)
int i2c_write_timeout_us(i2c_inst_t *i2c, uint8_t addr, const uint8_t *src, size_t len, bool nostop, unsigned timeout_us);
int i2c_read_timeout_us(i2c_inst_t *i2c, uint8_t addr, uint8_t *dst, size_t len, bool nostop, unsigned timeout_us);
