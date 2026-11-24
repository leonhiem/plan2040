/**
 * apps/babywarmer/dev/setpoint.cpp — /dev/setpoint, /dev/setpoint/raised
 *
 *   cat /dev/setpoint          the skin setpoint, e.g. "35.0"
 *   echo 36.5 > /dev/setpoint  set it, within SETPOINT_MIN..MAX (30-39 C)
 *   cat /dev/setpoint/raised   how many times it was raised by RAISE_C (2 C)
 *                              or more
 *
 * A raise counts when the presses stop (EDIT_SETTLE_MS, 5 s, after the
 * last change), from the value before the first press. warmer boosts again
 * on it when the skin is far below, and the guard then watches the
 * warm-up. A counter, so both see each raise without taking it from the
 * other. UP/DOWN reach the setpoint through `adjust` in the boot script.
 */
#include "kernel/fs.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstdlib>

#define SETPOINT_MIN     30.0f
#define SETPOINT_MAX     39.0f
#define SETPOINT_DEF     35.0f

#define RAISE_C          2.0f
#define EDIT_SETTLE_MS   5000

static float setpoint = SETPOINT_DEF;

static bool  editing;            // a session of changes, not settled yet
static float before_edit;
static absolute_time_t last_edit;
static int   raised;

static void settle(void)
{
    if (!editing || absolute_time_diff_us(last_edit, get_absolute_time()) < (int64_t)EDIT_SETTLE_MS * 1000) return;
    editing = false;
    if (setpoint - before_edit >= RAISE_C) raised++;
}

static void clamp(void)
{
    if (setpoint < SETPOINT_MIN) setpoint = SETPOINT_MIN;
    if (setpoint > SETPOINT_MAX) setpoint = SETPOINT_MAX;
}

static int setpoint_read(char *buf, int len)
{
    return snprintf(buf, len, "%.1f\n", setpoint);
}

static int setpoint_write(const char *buf, int len)
{
    char tmp[16];
    int n = len < (int)sizeof(tmp) - 1 ? len : (int)sizeof(tmp) - 1;
    for (int i = 0; i < n; i++) tmp[i] = buf[i];
    tmp[n] = '\0';

    char *end;
    float v = strtof(tmp, &end);
    if (end == tmp) return -1; // not a number

    settle();
    if (!editing) { editing = true; before_edit = setpoint; }
    last_edit = get_absolute_time();
    setpoint = v;
    clamp();
    return len;
}

static int raised_read(char *buf, int len)
{
    settle();
    return snprintf(buf, len, "%d\n", raised);
}

static const device_t dev_raised = { "/dev/setpoint/raised", 0, 0, raised_read, 0 };

static const device_t dev_setpoint = {
    "/dev/setpoint",
    0,
    0,
    setpoint_read,
    setpoint_write,
};

void dev_setpoint_register(void)
{
    fs_register(&dev_setpoint);
    fs_register(&dev_raised);
}
