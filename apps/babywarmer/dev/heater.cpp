/**
 * apps/babywarmer/dev/heater.cpp — /dev/heater: read/write, TPO heater power 0-100
 *
 * write /dev/heater 25   -> heater_set_power(25.0), clamped [0,100]
 * write /dev/heater on   -> heater_set_power(100)   (alias)
 * write /dev/heater off  -> heater_set_power(0)      (alias)
 * cat /dev/heater        -> the power applied, e.g. "25.0\n" (since
 *                           2026-11-24 within the guard's say, see
 *                           /dev/heater/limit; before: the last write)
 *
 * on/off are just two more accepted spellings of the same 0-100 power
 * value (100 and 0), not a second mode -- still one write(), no side
 * channel -- so thresh/hyst's "on"/"off" output can drive this device
 * directly, same as it drives /dev/lamp/alarm/relay.
 *
 * This drives a real SSR and real heat output via tpo_apply() (heater.cpp),
 * which main() registers as a task on its own schedule -- writing here
 * takes effect on tpo_apply()'s next tick, not instantly. No safety
 * interlock beyond the 0-100 clamp: deliberately raw, matching the
 * rest of this experimental line.
 *
 * read() added so prog/monitor.cpp can show the actually-applied value
 * (heater_get_power(), heater.cpp) rather than /dev/pidout/percent's
 * own value -- those are what a *source* is computing, this is what's
 * really reaching the SSR right now, which can matter if a wiring
 * mistake leaves the two disagreeing.
 *
 * From picoos @ 89c16e5 (dev/heater.cpp). plan2040 (2026-11-21):
 * dev_heater_register() also sets up the bargraph PWM and registers the
 * "tpo" kernel task (100 ms) that applies the power, which picoos's
 * main() did; and /dev/heater/ssr (2026-11-23, see below); the rest
 * unchanged. The heater only really heats with the safety relay
 * energized too (/dev/relay on).
 */
#include "board.h"
#include "heater.h"
#include "kernel/task.h"
#include "kernel/fs.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int heater_read(char *buf, int len)
{
    return snprintf(buf, len, "%.1f\n", heater_get_power());
}

static int heater_write(const char *buf, int len)
{
    if (len >= 2 && strncmp(buf, "on", 2) == 0)  { heater_set_power(100.0f); return len; }
    if (len >= 3 && strncmp(buf, "off", 3) == 0) { heater_set_power(0.0f);   return len; }

    char tmp[16];
    int n = len < (int)sizeof(tmp) - 1 ? len : (int)sizeof(tmp) - 1;
    for (int i = 0; i < n; i++) tmp[i] = buf[i];
    tmp[n] = '\0';

    char *end;
    float pct = strtof(tmp, &end);
    if (end == tmp) return -1; // not a number, and not on/off either

    heater_set_power(pct);
    return len;
}

// /dev/heater/ssr (plan2040, 2026-11-23): read-only, the SSR right now and
// how long it has been so, in ms: "on 1300" / "off 200". What the
// heater-current check needs to judge a settled state (prog/alarmcheck).
static int ssr_read(char *buf, int len)
{
    uint32_t ms;
    bool on = heater_ssr(&ms);
    return snprintf(buf, len, "%s %lu\n", on ? "on" : "off", (unsigned long)ms);
}
static const device_t dev_heater_ssr = { "/dev/heater/ssr", 0, 0, ssr_read, 0 };

// /dev/heater/limit (plan2040, 2026-11-24): read-only, the guard's say
// (dev/guard.cpp): "cap 100.0" (the jobs' power, at most that), or
// "set 39.0" (the guard's own power, whatever the jobs ask).
static int limit_read(char *buf, int len)
{
    float v;
    bool set = heater_guard_get(&v);
    return snprintf(buf, len, "%s %.1f\n", set ? "set" : "cap", v);
}
static const device_t dev_heater_limit = { "/dev/heater/limit", 0, 0, limit_read, 0 };

static const device_t dev_heater = {
    "/dev/heater",
    0,
    0,
    heater_read,
    heater_write,
};

void dev_heater_register(void)
{
    heater_set_power(0.0f);
    heater_pwm_init();
    task_register("tpo", tpo_apply, TPO_INTERVAL_TIME); // applies /dev/heater's value
    fs_register(&dev_heater);
    fs_register(&dev_heater_ssr);
    fs_register(&dev_heater_limit);
}
