// drivers/pcf8563.cpp — see pcf8563.h. A failure says why (errstr,
// drivers/README.md rule 3, 2026-11-24): "RTC (0x51, local bus) not
// answering", or what's wrong with a date.
#include "pcf8563.h"
#include "board.h"
#include "kernel/errstr.h"
#include <cstdio>

#define I2C_TIMEOUT_US 10000

static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0f); }
static uint8_t bin2bcd(int v)     { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static bool no_answer(void)
{
    werrstr("RTC (0x%02x, %s) not answering", RTC_I2C_ADDR, RTC_I2C_NAME);
    return false;
}

bool rtc_read_regs(uint8_t reg, uint8_t *buf, int len)
{
    if (i2c_write_timeout_us(RTC_I2C, RTC_I2C_ADDR, &reg, 1, true, I2C_TIMEOUT_US) != 1
        || i2c_read_timeout_us(RTC_I2C, RTC_I2C_ADDR, buf, len, false, I2C_TIMEOUT_US) != len)
        return no_answer();
    return true;
}

bool rtc_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = { reg, val };
    if (i2c_write_timeout_us(RTC_I2C, RTC_I2C_ADDR, b, 2, false, I2C_TIMEOUT_US) != 2)
        return no_answer();
    return true;
}

bool rtc_init(void)
{
    return rtc_write_reg(0x00, 0x00)    // control_1: running, normal mode
        && rtc_write_reg(0x01, 0x00)    // control_2: no alarm/timer flags or interrupts
        && rtc_write_reg(0x0d, 0x00);   // CLKOUT_control: FE=0, output off
}

bool rtc_get(rtc_time *t)
{
    uint8_t r[7];
    if (!rtc_read_regs(0x02, r, 7))
        return false;
    t->vl      = r[0] & 0x80;
    t->sec     = bcd2bin(r[0] & 0x7f);
    t->min     = bcd2bin(r[1] & 0x7f);
    t->hour    = bcd2bin(r[2] & 0x3f);
    t->day     = bcd2bin(r[3] & 0x3f);
    t->weekday = r[4] & 0x07;
    t->month   = bcd2bin(r[5] & 0x1f);
    t->year    = 2000 + bcd2bin(r[6]) + ((r[5] & 0x80) ? 100 : 0);
    return true;
}

bool rtc_valid(const rtc_time *t)
{
    return t->sec < 60 && t->min < 60 && t->hour < 24 && t->day >= 1 && t->day <= 31
        && t->month >= 1 && t->month <= 12;
}

// 0 = Sunday (Sakamoto)
int rtc_weekday_of(int y, int m, int d)
{
    static const int k[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3)
        y--;
    return (y + y / 4 - y / 100 + y / 400 + k[m - 1] + d) % 7;
}

bool rtc_set(const rtc_time *t)
{
    uint8_t b[8] = {
        0x02,
        bin2bcd(t->sec),            // writing seconds clears VL
        bin2bcd(t->min),
        bin2bcd(t->hour),
        bin2bcd(t->day),
        (uint8_t)rtc_weekday_of(t->year, t->month, t->day),
        (uint8_t)(bin2bcd(t->month) | (t->year >= 2100 ? 0x80 : 0)),
        bin2bcd(t->year % 100),
    };
    // Stop the clock while writing so the new second starts cleanly.
    if (!rtc_write_reg(0x00, 0x20))
        return false;
    bool ok = i2c_write_timeout_us(RTC_I2C, RTC_I2C_ADDR, b, 8, false, I2C_TIMEOUT_US) == 8;
    bool restarted = rtc_write_reg(0x00, 0x00);
    if (!ok || !restarted) return no_answer();
    return true;
}

bool rtc_parse(const char *s, rtc_time *t)
{
    if (sscanf(s, "%d-%d-%d %d:%d:%d", &t->year, &t->month, &t->day,
               &t->hour, &t->min, &t->sec) != 6) {
        werrstr("not a date and time: want YYYY-MM-DD HH:MM:SS");
        return false;
    }
    if (t->year < 2000 || t->year > 2199 || !rtc_valid(t)) {
        werrstr("no such date or time (the year 2000-2199, month 1-12, day 1-31, 00:00:00-23:59:59)");
        return false;
    }
    return true;
}

int rtc_format(char *buf, int len, const rtc_time *t)
{
    return snprintf(buf, len, "%04d-%02d-%02d %02d:%02d:%02d",
                    t->year, t->month, t->day, t->hour, t->min, t->sec);
}
