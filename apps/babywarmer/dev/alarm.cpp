/**
 * apps/babywarmer/dev/alarm.cpp — /dev/alarm (the buzzer and alarm LED) and
 * the alarm conditions /dev/alarm/{heater,temphigh,templow,sensor,room,check}
 *
 *   echo on > /dev/alarm       buzzer + alarm LED on
 *   echo muted > /dev/alarm    LED on, buzzer off (MUTE: never silent and dark)
 *   echo off > /dev/alarm      both off
 *
 * The conditions are written by the guard (dev/guard.cpp), read by the jobs
 * alarm, ledwire and tftwire: "off", "warn: <why>" (the LEDs and the TFT
 * show it, no buzzer) or "on: <why>" (the buzzer too). check is manual
 * mode's "check the baby" (dev/guard.cpp).
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

// Each condition: off, warn (look: the LEDs and the TFT show it, no
// buzzer) or on (the buzzer too), with a reason: "warn: <why>", "on: <why>".
typedef struct { int level; char why[96]; } alarm_why_t;   // 0 off, 1 warn, 2 on
static alarm_why_t heater_alarm, sensor_alarm, room_alarm, check_alarm, temphigh_alarm, templow_alarm;

static int why_read(const alarm_why_t *a, char *buf, int len)
{
    static const char *word[] = { "off", "warn", "on" };
    if (a->level == 0 || !a->why[0]) return snprintf(buf, len, "%s\n", word[a->level]);
    return snprintf(buf, len, "%s: %s\n", word[a->level], a->why);
}

static int why_write(alarm_why_t *a, const char *buf, int len)
{
    int skip;
    if (len >= 4 && strncmp(buf, "warn", 4) == 0)     { a->level = 1; skip = 4; }
    else if (len >= 3 && strncmp(buf, "off", 3) == 0) { a->level = 0; skip = 3; }
    else if (len >= 2 && strncmp(buf, "on", 2) == 0)  { a->level = 2; skip = 2; }
    else return -1;
    a->why[0] = '\0';
    if (a->level && len > skip + 2 && buf[skip] == ':') {          // "<word>: <why>"
        int n = len - skip - 2 < (int)sizeof(a->why) - 1 ? len - skip - 2 : (int)sizeof(a->why) - 1;
        memcpy(a->why, buf + skip + 2, n);
        a->why[n] = '\0';
        char *nl = strchr(a->why, '\n');
        if (nl) *nl = '\0';
    }
    return len;
}

#define ALARM(name) \
    static int name##_read(char *buf, int len)        { return why_read(&name##_alarm, buf, len); } \
    static int name##_write(const char *buf, int len) { return why_write(&name##_alarm, buf, len); }
ALARM(heater) ALARM(sensor) ALARM(room) ALARM(check) ALARM(temphigh) ALARM(templow)

static const device_t dev_alarm_heater   = { "/dev/alarm/heater",   0, 0, heater_read,   heater_write };
static const device_t dev_alarm_temphigh = { "/dev/alarm/temphigh", 0, 0, temphigh_read, temphigh_write };
static const device_t dev_alarm_templow  = { "/dev/alarm/templow",  0, 0, templow_read,  templow_write };
static const device_t dev_alarm_sensor   = { "/dev/alarm/sensor",   0, 0, sensor_read,   sensor_write };
static const device_t dev_alarm_room     = { "/dev/alarm/room",     0, 0, room_read,     room_write };
static const device_t dev_alarm_check    = { "/dev/alarm/check",    0, 0, check_read,    check_write };

void dev_alarm_register(void)
{
    fs_register(&dev_alarm);
    fs_register(&dev_alarm_heater);
    fs_register(&dev_alarm_temphigh);
    fs_register(&dev_alarm_templow);
    fs_register(&dev_alarm_sensor);
    fs_register(&dev_alarm_room);
    fs_register(&dev_alarm_check);
}
