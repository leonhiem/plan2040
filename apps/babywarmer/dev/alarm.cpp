/**
 * apps/babywarmer/dev/alarm.cpp — /dev/alarm (the buzzer and alarm LED) and
 * the alarm conditions /dev/alarm/{heater,temphigh,templow,sensor}
 *
 *   echo on > /dev/alarm       buzzer + alarm LED on
 *   echo muted > /dev/alarm    LED on, buzzer off (MUTE: never silent and dark)
 *   echo off > /dev/alarm      both off
 *
 * The conditions are written by the guard (dev/guard.cpp), read by the jobs
 * alarm, ledwire and tftwire: "on"/"off"; heater and sensor also keep a
 * reason ("on: <why>"), which readers that only look for "on" ignore.
 *
 * GPIO: the alarm LED is active-low, the buzzer active-high (since the
 * 2026-08-26 buzzer circuit change). Both off at boot (board_pins_init()).
 * This device owns GPIO1, so a babywarmer config has no /dev/led.
 */
#include "board.h"
#include "kernel/fs.h"
#include "hardware/gpio.h"
#include <cstdio>
#include <cstring>

static int alarm_write(const char *buf, int len)
{
    if (len >= 2 && strncmp(buf, "on", 2) == 0) {
        gpio_put(PIN_ALARM, 1);
        gpio_put(PIN_ALARM_LED, 0);
        return len;
    }
    if (len >= 5 && strncmp(buf, "muted", 5) == 0) {
        gpio_put(PIN_ALARM, 0);
        gpio_put(PIN_ALARM_LED, 0);
        return len;
    }
    if (len >= 3 && strncmp(buf, "off", 3) == 0) {
        gpio_put(PIN_ALARM, 0);
        gpio_put(PIN_ALARM_LED, 1);
        return len;
    }
    return -1;
}

static const device_t dev_alarm = { "/dev/alarm", 0, 0, 0, alarm_write };   // write-only

// ---- the conditions ---------------------------------------------------------

static bool temphigh_fail = false;
static bool templow_fail  = false;

static int read_bool(char *buf, int len, bool v)  { return snprintf(buf, len, v ? "on\n" : "off\n"); }

static int write_bool(const char *buf, int len, bool *dst)
{
    if (len >= 2 && strncmp(buf, "on", 2) == 0)  { *dst = true;  return len; }
    if (len >= 3 && strncmp(buf, "off", 3) == 0) { *dst = false; return len; }
    return -1;
}

// heater and sensor keep their reason: write "on: <why>", read gives it
// back; "off" clears it.
typedef struct { bool on; char why[96]; } alarm_why_t;
static alarm_why_t heater_alarm, sensor_alarm;

static int why_read(const alarm_why_t *a, char *buf, int len)
{
    if (!a->on) return snprintf(buf, len, "off\n");
    return snprintf(buf, len, a->why[0] ? "on: %s\n" : "on\n", a->why);
}

static int why_write(alarm_why_t *a, const char *buf, int len)
{
    if (write_bool(buf, len, &a->on) < 0) return -1;
    a->why[0] = '\0';
    if (a->on && len > 4 && buf[2] == ':') {              // "on: <why>"
        int n = len - 4 < (int)sizeof(a->why) - 1 ? len - 4 : (int)sizeof(a->why) - 1;
        memcpy(a->why, buf + 4, n);
        a->why[n] = '\0';
        char *nl = strchr(a->why, '\n');
        if (nl) *nl = '\0';
    }
    return len;
}

static int heater_read(char *buf, int len)        { return why_read(&heater_alarm, buf, len); }
static int heater_write(const char *buf, int len) { return why_write(&heater_alarm, buf, len); }
static int sensor_read(char *buf, int len)        { return why_read(&sensor_alarm, buf, len); }
static int sensor_write(const char *buf, int len) { return why_write(&sensor_alarm, buf, len); }

static int temphigh_read(char *buf, int len)        { return read_bool(buf, len, temphigh_fail); }
static int temphigh_write(const char *buf, int len) { return write_bool(buf, len, &temphigh_fail); }

static int templow_read(char *buf, int len)         { return read_bool(buf, len, templow_fail); }
static int templow_write(const char *buf, int len)  { return write_bool(buf, len, &templow_fail); }

static const device_t dev_alarm_heater   = { "/dev/alarm/heater",   0, 0, heater_read,   heater_write };
static const device_t dev_alarm_temphigh = { "/dev/alarm/temphigh", 0, 0, temphigh_read, temphigh_write };
static const device_t dev_alarm_templow  = { "/dev/alarm/templow",  0, 0, templow_read,  templow_write };
static const device_t dev_alarm_sensor   = { "/dev/alarm/sensor",   0, 0, sensor_read,   sensor_write };

void dev_alarm_register(void)
{
    fs_register(&dev_alarm);
    fs_register(&dev_alarm_heater);
    fs_register(&dev_alarm_temphigh);
    fs_register(&dev_alarm_templow);
    fs_register(&dev_alarm_sensor);
}
