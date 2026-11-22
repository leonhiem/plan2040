#include "ads1115.h"
#include "board.h"
#include "kernel/errstr.h"
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#define I2C_TIMEOUT_US 10000

#define REG_CONVERSION 0x00
#define REG_CONFIG     0x01

#define CFG_OS         0x8000   // write: start a conversion; read: 1 = not converting
#define CFG_MODE_SINGLE 0x0100  // single-shot (power-down after each conversion)
#define CFG_COMP_OFF   0x0003   // comparator disabled (ALERT/RDY pin high-Z)

static bool no_answer(void)
{
    werrstr("ADS1115 (0x%02x, %s) not answering", ADS1115_I2C_ADDR, ADS1115_I2C_NAME);
    return false;
}

static bool write_reg(uint8_t reg, uint16_t v)
{
    uint8_t b[3] = { reg, (uint8_t)(v >> 8), (uint8_t)v };
    return i2c_write_timeout_us(ADS1115_I2C, ADS1115_I2C_ADDR, b, 3, false, I2C_TIMEOUT_US) == 3;
}

static bool read_reg(uint8_t reg, uint16_t *v)
{
    uint8_t b[2];
    if (i2c_write_timeout_us(ADS1115_I2C, ADS1115_I2C_ADDR, &reg, 1, true, I2C_TIMEOUT_US) != 1)
        return false;
    if (i2c_read_timeout_us(ADS1115_I2C, ADS1115_I2C_ADDR, b, 2, false, I2C_TIMEOUT_US) != 2)
        return false;
    *v = (uint16_t)(b[0] << 8 | b[1]);
    return true;
}

bool ads1115_start(int mux, int pga, int rate)
{
    uint16_t cfg = CFG_OS | (mux & 7) << 12 | (pga & 7) << 9 | CFG_MODE_SINGLE
                 | (rate & 7) << 5 | CFG_COMP_OFF;
    return write_reg(REG_CONFIG, cfg) || no_answer();
}

int ads1115_ready(void)
{
    uint16_t cfg;
    if (!read_reg(REG_CONFIG, &cfg)) { no_answer(); return -1; }
    return (cfg & CFG_OS) ? 1 : 0;
}

bool ads1115_result(int16_t *code)
{
    uint16_t v;
    if (!read_reg(REG_CONVERSION, &v)) return no_answer();
    *code = (int16_t)v;
    return true;
}

bool ads1115_convert(int mux, int pga, int rate, int16_t *code)
{
    static const int rate_sps[8] = { 8, 16, 32, 64, 128, 250, 475, 860 };
    if (!ads1115_start(mux, pga, rate)) return false;
    absolute_time_t deadline = make_timeout_time_us(1000000 / rate_sps[rate & 7] + 10000);
    for (;;) {
        int r = ads1115_ready();
        if (r < 0) return false;
        if (r == 1) break;
        if (time_reached(deadline)) {
            werrstr("ADS1115 (0x%02x, %s): conversion never finished", ADS1115_I2C_ADDR, ADS1115_I2C_NAME);
            return false;
        }
        sleep_us(500);
    }
    return ads1115_result(code);
}
