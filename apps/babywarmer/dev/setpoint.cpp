/**
 * apps/babywarmer/dev/setpoint.cpp — /dev/setpoint: read/write, the temperature setpoint
 *
 * cat /dev/setpoint          -> current value, e.g. "35.0\n"
 * echo 36.5 > /dev/setpoint  -> set directly, clamped to [SETPOINT_MIN, SETPOINT_MAX]
 *
 * No longer wired to the UP/DOWN buttons directly -- that used to be a
 * dedicated task_setpoint_buttons registered from main(), hardcoding
 * "UP/DOWN always adjusts the setpoint." Replaced by prog/adjust.cpp,
 * a generic program you run from the command line to decide what
 * UP/DOWN adjusts and under what condition, e.g.:
 *
 *   adjust /dev/heaterauto /dev/setpoint /dev/percent 0.5 &
 *
 * This device now only stores and clamps the value; it doesn't know or
 * care what's driving it.
 *
 * /dev/setpoint/raised (2026-11-24, TODO.md idea 3 step 5, from the
 * field-tested warmer.cpp, 2026-04-27): how many times the setpoint has
 * been raised by RAISE_C or more, counted when the nurse stops pressing
 * (EDIT_SETTLE_MS after the last change; one session of presses counts
 * as one change, from the value before the first). A raise that large
 * needs a real warm-up, not the PID's slow climb: prog/phase.cpp goes
 * back to boost, and the guard (dev/guard.cpp) watches it as a warm-up
 * again. A counter, not a flag, so both can see each raise without
 * taking it from the other.
 *
 * From picoos @ 89c16e5 (dev/setpoint.cpp), unchanged apart from the includes
 * and the register function's name (2026-11-22).
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
