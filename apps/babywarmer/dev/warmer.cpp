/**
 * apps/babywarmer/dev/warmer.cpp — the devices of the warmer's control
 * (prog/warmer.cpp)
 *
 *   /dev/state                 the phase: idle|boost|coast|pid|safe (written
 *                              by warmer; other words are refused)
 *   /dev/autopower             the power auto mode asks for, 0-100 (warmer's
 *                              output; `follow` sends it to the heater)
 *   /dev/pid/kp, ti, td        the PID's tuning: 3.0 %/C, 200 s, 5 s (the
 *                              field warmer's, tuned on its logs)
 *   /dev/pid/min, max          the PID's part around the feed-forward:
 *                              -35..+25 %
 *   /dev/pid/outmax            feed-forward + PID at most 75 %
 *   /dev/pid/integral, ff      what warmer uses, published every second
 *                              (for cat and monitor; a write is overwritten)
 *
 * The tuning is read by warmer every second: change it while running.
 */
#include "kernel/fs.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static const char *STATE_NAMES[] = { "idle", "boost", "coast", "pid", "safe" };
#define STATE_COUNT (int)(sizeof(STATE_NAMES) / sizeof(STATE_NAMES[0]))

static char  state[8] = "idle";
static float autopower;
static float kp = 3.0f, ti = 200.0f, td = 5.0f;
static float pmin = -35.0f, pmax = 25.0f, outmax = 75.0f;
static float integral, ff;

// A number into *dst, within lo..hi; -1 if it isn't one.
static int write_float(const char *buf, int len, float *dst, float lo, float hi)
{
    char tmp[16];
    int n = len < (int)sizeof(tmp) - 1 ? len : (int)sizeof(tmp) - 1;
    memcpy(tmp, buf, n);
    tmp[n] = '\0';
    char *end;
    float v = strtof(tmp, &end);
    if (end == tmp) return -1;
    *dst = v < lo ? lo : v > hi ? hi : v;
    return len;
}

static int state_read(char *buf, int len) { return snprintf(buf, len, "%s\n", state); }
static int state_write(const char *buf, int len)
{
    for (int i = 0; i < STATE_COUNT; i++) {
        size_t n = strlen(STATE_NAMES[i]);
        if ((size_t)len >= n && strncmp(buf, STATE_NAMES[i], n) == 0) {
            strcpy(state, STATE_NAMES[i]);
            return len;
        }
    }
    return -1;
}

#define FLOAT_DEV(name, var, lo, hi, fmt)                                              \
    static int name##_read(char *buf, int len) { return snprintf(buf, len, fmt "\n", var); } \
    static int name##_write(const char *buf, int len) { return write_float(buf, len, &var, lo, hi); }

FLOAT_DEV(autopower, autopower, 0.0f, 100.0f, "%.1f")
FLOAT_DEV(kp,        kp,        0.0f, 100.0f, "%.2f")
FLOAT_DEV(ti,        ti,        1.0f, 10000.0f, "%.1f")   // Kp/Ti: never 0
FLOAT_DEV(td,        td,        0.0f, 1000.0f, "%.1f")
FLOAT_DEV(pmin,      pmin,   -100.0f, 0.0f,   "%.1f")
FLOAT_DEV(pmax,      pmax,      0.0f, 100.0f, "%.1f")
FLOAT_DEV(outmax,    outmax,    0.0f, 100.0f, "%.1f")
FLOAT_DEV(integral,  integral, -100.0f, 100.0f, "%.3f")
FLOAT_DEV(ff,        ff,        0.0f, 100.0f, "%.1f")

static const device_t devs[] = {
    { "/dev/state",        0, 0, state_read,     state_write },
    { "/dev/autopower",    0, 0, autopower_read, autopower_write },
    { "/dev/pid/kp",       0, 0, kp_read,        kp_write },
    { "/dev/pid/ti",       0, 0, ti_read,        ti_write },
    { "/dev/pid/td",       0, 0, td_read,        td_write },
    { "/dev/pid/min",      0, 0, pmin_read,      pmin_write },
    { "/dev/pid/max",      0, 0, pmax_read,      pmax_write },
    { "/dev/pid/outmax",   0, 0, outmax_read,    outmax_write },
    { "/dev/pid/integral", 0, 0, integral_read,  integral_write },
    { "/dev/pid/ff",       0, 0, ff_read,        ff_write },
};

void dev_warmer_register(void)
{
    for (unsigned i = 0; i < sizeof(devs) / sizeof(devs[0]); i++) fs_register(&devs[i]);
}
