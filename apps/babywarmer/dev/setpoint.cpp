/**
 * apps/babywarmer/dev/setpoint.cpp — /dev/setpoint, /dev/setpoint/raised
 *
 *   cat /dev/setpoint          the skin setpoint, e.g. "35.0"
 *   echo 36.5 > /dev/setpoint  set it, within 30.0-37.5 C
 *   cat /dev/setpoint/high     "on" while above 37.5 C is allowed (up to 39.0)
 *
 * Above 37.5 C only on purpose (IEC 60601-2-21, as commercial warmers): at
 * 37.5, hold UP for 3 s. UP repeats while held, so `adjust` keeps writing
 * past the limit; HIGH_HOLD_MS of such writes, each within HIGH_GAP_MS of
 * the last, allow up to 39.0 (the field warmer's maximum). From the shell:
 * `echo on > /dev/setpoint/high`. Back at 37.5 or below, the limit returns.
 * ledwire blinks the small display while it's above 37.5.
 *   cat /dev/setpoint/raised   how many times it was raised (by RAISE_C,
 *                              0.1 C, or more)
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
#include <cstring>

#define SETPOINT_MIN     30.0f   // Leon, 2026-11-26 (was 34.0, the GE Panda warmer's)
#define SETPOINT_NORMAL  37.5f   // the limit ...
#define SETPOINT_MAX     39.0f   // ... and above it, on purpose
#define SETPOINT_DEF     35.0f   // the field warmer's
#define HIGH_HOLD_MS     3000    // pushing past 37.5 this long ...
#define HIGH_GAP_MS      400     // ... with writes this close together (UP's repeat: 150 ms)

#define RAISE_C          0.1f    // any raise (was 2 C: a raise of 1.5 C with the skin far
                                 // below looked like a sensor off the baby, 2026-11-26)
#define EDIT_SETTLE_MS   5000

static float setpoint = SETPOINT_DEF;
static bool  high;               // above SETPOINT_NORMAL allowed
static bool  pushing;            // writes past the limit, close together
static absolute_time_t push_start, push_last;

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
    if (setpoint > SETPOINT_NORMAL && !high) {
        absolute_time_t now = get_absolute_time();
        if (!pushing || absolute_time_diff_us(push_last, now) > (int64_t)HIGH_GAP_MS * 1000) {
            pushing = true;
            push_start = now;
        } else if (absolute_time_diff_us(push_start, now) >= (int64_t)HIGH_HOLD_MS * 1000) {
            high = true;
        }
        push_last = now;
    }
    if (setpoint < SETPOINT_MIN) setpoint = SETPOINT_MIN;
    if (setpoint > (high ? SETPOINT_MAX : SETPOINT_NORMAL)) setpoint = high ? SETPOINT_MAX : SETPOINT_NORMAL;
    if (setpoint <= SETPOINT_NORMAL) { high = false; if (setpoint < SETPOINT_NORMAL) pushing = false; }
}

static int high_read(char *buf, int len) { return snprintf(buf, len, high ? "on\n" : "off\n"); }
static int high_write(const char *buf, int len)
{
    if (len >= 2 && strncmp(buf, "on", 2) == 0)       high = true;
    else if (len >= 3 && strncmp(buf, "off", 3) == 0) { high = false; clamp(); }
    else return -1;
    return len;
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
static const device_t dev_high   = { "/dev/setpoint/high",   0, 0, high_read,   high_write };

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
    fs_register(&dev_high);
}
