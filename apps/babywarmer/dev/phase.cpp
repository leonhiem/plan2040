/**
 * apps/babywarmer/dev/phase.cpp — /dev/state, /dev/autopower, /dev/safepower,
 * /dev/ffpower
 *
 * The phase state machine's own devices, bundled in one file the same
 * reason dev/pid.cpp bundles its own tunables -- one logical unit,
 * even though each is independently addressable.
 *
 * /dev/state: read/write, one of idle|boost|coast|pid|safe. Written
 * only by prog/phase.cpp (its transition engine); just stores and
 * clamps to that vocabulary here, same "doesn't know what's driving
 * it" shape as every other device -- writing an unrecognized word is
 * rejected rather than silently accepted.
 *
 * /dev/autopower: whatever the currently-active phase says heater
 * power should be. Written by prog/select.cpp, read by follow as the
 * auto-mode source in the unchanged top-level wiring:
 *
 *   follow /dev/heaterauto /dev/autopower /dev/percent /dev/heater &
 *
 * /dev/safepower: safe mode's lookup-table output, written by
 * prog/safelut.cpp, read by prog/select.cpp when /dev/state is safe.
 * Kept separate from /dev/pidout -- safe mode isn't PID, it's a
 * different control law with a different owner.
 *
 * /dev/ffpower (2026-11-24, TODO.md idea 3 step 5): the feed-forward for
 * the room temperature, written by prog/fftable.cpp, read by prog/pid.cpp
 * as its bias. When the room sensor fails nothing writes it; a read then
 * gives the last value while it's younger than FF_FRESH_MS (a room
 * changes slowly), after that FF_FALLBACK (the table's value at 24 C, a
 * conservative middle; the field warmer used 50 %, a high bias in a warm
 * room). The guard says the room sensor is down (/dev/alarm/sensor).
 *
 * From picoos @ 89c16e5 (dev/phase.cpp), unchanged apart from the includes
 * and the register function's name (2026-11-22).
 */
#include "kernel/fs.h"
#include "pico/stdlib.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

static const char *STATE_NAMES[] = { "idle", "boost", "coast", "pid", "safe" };
#define STATE_COUNT (int)(sizeof(STATE_NAMES) / sizeof(STATE_NAMES[0]))

static char state[8] = "idle";
static float autopower = 0.0f;
static float safepower = 0.0f;

#define FF_FRESH_MS  (10 * 60 * 1000)
#define FF_FALLBACK  30.0f
static float ffpower;
static bool  ff_written;
static absolute_time_t ff_when;

static int state_read(char *buf, int len)
{
    return snprintf(buf, len, "%s\n", state);
}

static int state_write(const char *buf, int len)
{
    for (int i = 0; i < STATE_COUNT; i++) {
        size_t n = strlen(STATE_NAMES[i]);
        if ((size_t)len >= n && strncmp(buf, STATE_NAMES[i], n) == 0) {
            strcpy(state, STATE_NAMES[i]);
            return len;
        }
    }
    return -1; // not one of idle/boost/coast/pid/safe
}

static int write_float(const char *buf, int len, float *dst)
{
    char tmp[16];
    int n = len < (int)sizeof(tmp) - 1 ? len : (int)sizeof(tmp) - 1;
    for (int i = 0; i < n; i++) tmp[i] = buf[i];
    tmp[n] = '\0';

    char *end;
    float v = strtof(tmp, &end);
    if (end == tmp) return -1;
    if (v < 0.0f)   v = 0.0f;
    if (v > 100.0f) v = 100.0f;
    *dst = v;
    return len;
}

static int autopower_read(char *buf, int len)        { return snprintf(buf, len, "%.1f\n", autopower); }
static int autopower_write(const char *buf, int len) { return write_float(buf, len, &autopower); }

static int safepower_read(char *buf, int len)        { return snprintf(buf, len, "%.1f\n", safepower); }
static int safepower_write(const char *buf, int len) { return write_float(buf, len, &safepower); }

static int ffpower_read(char *buf, int len)
{
    bool fresh = ff_written && absolute_time_diff_us(ff_when, get_absolute_time()) < (int64_t)FF_FRESH_MS * 1000;
    return snprintf(buf, len, "%.1f\n", fresh ? ffpower : FF_FALLBACK);
}
static int ffpower_write(const char *buf, int len)
{
    int r = write_float(buf, len, &ffpower);
    if (r >= 0) { ff_written = true; ff_when = get_absolute_time(); }
    return r;
}

static const device_t dev_state      = { "/dev/state",      0, 0, state_read,      state_write };
static const device_t dev_autopower  = { "/dev/autopower",  0, 0, autopower_read,  autopower_write };
static const device_t dev_safepower  = { "/dev/safepower",  0, 0, safepower_read,  safepower_write };
static const device_t dev_ffpower    = { "/dev/ffpower",    0, 0, ffpower_read,    ffpower_write };

void dev_phase_register(void)
{
    fs_register(&dev_state);
    fs_register(&dev_autopower);
    fs_register(&dev_safepower);
    fs_register(&dev_ffpower);
}
